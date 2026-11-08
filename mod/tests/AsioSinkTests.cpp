#include "audio_v2/AsioExclusiveSink.h"
#include "AsioDevices.h"
#include "AudioErrors.h"

#include <array>
#include <atomic>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ammod;
using namespace ammod::audio_v2;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate>
void WaitFor(Predicate predicate, const char* message) {
    const auto deadline = GetTickCount64() + 3500;
    while (!predicate() && GetTickCount64() < deadline) Sleep(2);
    Require(predicate(), message);
}

struct DriverPlan {
    long type{18};
    bool rejectRate{}, failCreate{}, failStart{}, stall{}, freezeClock{};
    long notification{};
    std::atomic<unsigned> instances{}, releases{}, starts{}, stops{}, disposals{}, wrongThread{}, callbacks{}, nonzeroPackets{};
    std::atomic<bool> fireNotification{};
    std::atomic<long> left{-1}, right{-1};
    std::atomic<double> openedRate{};
};

DriverPlan* plan{};

class FakeDriver final : public asio::Driver {
public:
    FakeDriver() : plan_(*plan), owner_(GetCurrentThreadId()) { ++plan_.instances; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        *object = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        CheckThread();
        const auto remaining = --references_;
        if (!remaining) { ++plan_.releases; delete this; }
        return remaining;
    }
    long init(void*) override { CheckThread(); return 1; }
    void getDriverName(char* name) override { strcpy_s(name, 32, "AMExclusive test driver"); }
    long getDriverVersion() override { return 2; }
    void getErrorMessage(char* message) override { strcpy_s(message, 124, "test error"); }
    long start() override {
        CheckThread();
        if (plan_.failStart) return -999;
        ++plan_.starts;
        stop_.store(false);
        position_.store(0);
        thread_ = std::thread([this] {
            long index{};
            while (!stop_.load()) {
                if (plan_.fireNotification.exchange(false)) {
                    if (plan_.notification == 100) callbacks_.sampleRateChanged(rate_ + 100);
                    else callbacks_.message(plan_.notification, 0, nullptr, nullptr);
                }
                if (!plan_.stall) {
                    if (!plan_.freezeClock) position_.fetch_add(static_cast<std::uint64_t>(frames_));
                    callbacks_.bufferSwitch(index, 1);
                    ++plan_.callbacks;
                    bool nonzero{};
                    for (const auto byte : storage_[index]) nonzero = nonzero || byte != 0;
                    if (nonzero) ++plan_.nonzeroPackets;
                    index = 1 - index;
                }
                Sleep(2);
            }
        });
        return 0;
    }
    long stop() override {
        CheckThread();
        stop_.store(true);
        if (thread_.joinable()) { thread_.join(); ++plan_.stops; }
        return 0;
    }
    long getChannels(long* inputs, long* outputs) override { CheckThread(); *inputs = 0; *outputs = 8; return 0; }
    long getLatencies(long* input, long* output) override { *input = 0; *output = frames_ * 2; return 0; }
    long getBufferSize(long* minimum, long* maximum, long* preferred, long* granularity) override {
        CheckThread(); *minimum = 32; *maximum = 512; *preferred = 64; *granularity = -1; return 0;
    }
    long canSampleRate(double rate) override { return rate == 48000 || (!plan_.rejectRate && rate == 44100) ? 0 : -997; }
    long getSampleRate(double* rate) override { *rate = rate_; return 0; }
    long setSampleRate(double rate) override {
        CheckThread();
        if (canSampleRate(rate)) return -997;
        rate_ = rate;
        plan_.openedRate.store(rate);
        return 0;
    }
    long getClockSources(asio::ClockSource*, long*) override { return -1000; }
    long setClockSource(long) override { return -1000; }
    long getSamplePosition(asio::Position* samples, asio::Position* timestamp) override {
        const auto position = position_.load();
        *samples = {static_cast<std::uint32_t>(position >> 32), static_cast<std::uint32_t>(position)};
        *timestamp = {};
        return 0;
    }
    long getChannelInfo(asio::ChannelInfo* info) override { CheckThread(); info->sampleType = plan_.type; return 0; }
    long createBuffers(asio::BufferInfo* buffers, long channels, long frames, asio::Callbacks* callbacks) override {
        CheckThread();
        if (plan_.failCreate) return -999;
        if (channels != 2) return -998;
        plan_.left.store(buffers[0].channel);
        plan_.right.store(buffers[1].channel);
        callbacks_ = *callbacks;
        frames_ = frames;
        const auto bytes = asio::Layout(plan_.type).bytes;
        for (long channel = 0; channel < channels; ++channel) {
            for (unsigned index = 0; index < 2; ++index) {
                auto& buffer = storage_[static_cast<std::size_t>(channel) * 2 + index];
                buffer.assign(static_cast<std::size_t>(frames) * bytes, 0);
                buffers[channel].buffers[index] = buffer.data();
            }
        }
        Require(callbacks_.message(2, 0, nullptr, nullptr) == 2, "ASIO engine version");
        Require(callbacks_.message(7, 0, nullptr, nullptr) == 0, "unexpected time info support");
        return 0;
    }
    long disposeBuffers() override { CheckThread(); ++plan_.disposals; for (auto& buffer : storage_) buffer.clear(); return 0; }
    long controlPanel() override { CheckThread(); return 0; }
    long future(long, void*) override { return -1000; }
    long outputReady() override { return 0; }

private:
    ~FakeDriver() { stop(); }
    void CheckThread() { if (GetCurrentThreadId() != owner_) ++plan_.wrongThread; }
    DriverPlan& plan_;
    DWORD owner_{};
    ULONG references_{1};
    double rate_{48000};
    long frames_{};
    asio::Callbacks callbacks_{};
    std::array<std::vector<unsigned char>, 4> storage_;
    std::thread thread_;
    std::atomic<bool> stop_{};
    std::atomic<std::uint64_t> position_{};
};

HRESULT Factory(const CLSID&, asio::Driver** driver) { *driver = new FakeDriver; return S_OK; }

WasapiSinkConfig Config() {
    WasapiSinkConfig config;
    config.format = {48000, 2, 24, 32, PcmEncoding::SignedInteger, true, 24};
    config.periodFrames = 64;
    config.endpointId = L"asio:{9C548082-B1CE-4BA1-9AD1-764F6FBC6F31}/2/3";
    return config;
}

class Source final : public IIntegerPcmSource {
public:
    std::atomic<bool> ready{true}, sealed{};
    std::atomic<std::uint32_t> fills{};
    std::uint32_t terminalFrames{17};
    HRESULT Prepare(std::uint32_t) noexcept override {
        if (!ready.load()) return kAudioSourceWouldBlock;
        return exhausted_.load() ? S_FALSE : S_OK;
    }
    HRESULT Fill(std::int32_t* output, std::uint32_t capacity, std::uint32_t& written, bool& end) noexcept override {
        ++fills;
        end = sealed.load();
        written = end ? std::min(capacity, terminalFrames) : capacity;
        for (std::uint32_t frame = 0; frame < written; ++frame) {
            output[frame * 2] = 0x40000000;
            output[frame * 2 + 1] = -0x40000000;
        }
        if (end) exhausted_.store(true);
        return S_OK;
    }
private:
    std::atomic<bool> exhausted_{};
};

void Packing() {
    const std::int32_t stereo[]{0x40000000, -0x40000000, INT32_MIN, 0x7fffff00};
    std::array<unsigned char, 16> output{};
    Require(asio::PackChannel(stereo, output.data(), 2, 0, asio::Layout(17)), "pack int24");
    Require(output[0] == 0 && output[1] == 0 && output[2] == 0x40 && output[5] == 0x80, "int24 LSB bytes");
    asio::PackChannel(stereo, output.data(), 2, 1, asio::Layout(1));
    Require(output[0] == 0xc0 && output[1] == 0 && output[2] == 0 && output[3] == 0x7f && output[5] == 0xff, "int24 MSB bytes");
    asio::PackChannel(stereo, output.data(), 1, 1, asio::Layout(27));
    Require(output[0] == 0 && output[1] == 0 && output[2] == 0xc0 && output[3] == 0xff, "right aligned int24 sign extension");
    asio::PackChannel(stereo, output.data(), 1, 0, asio::Layout(19));
    float single{};
    std::memcpy(&single, output.data(), sizeof(single));
    Require(single == 0.5f, "float32 normalization");
    asio::PackChannel(stereo, output.data(), 1, 1, asio::Layout(20));
    double wide{};
    std::memcpy(&wide, output.data(), sizeof(wide));
    Require(wide == -0.5, "float64 normalization");
    Require(asio::Layout(32).bytes == 0 && asio::Layout(26).bytes == 0, "unsupported format accepted");
    Require(asio::SelectBufferFrames(32, 2048, 256, -1, 1000) == 1024, "power of two size");
    Require(asio::SelectBufferFrames(48, 384, 96, 48, 110) == 96, "stepped buffer size");
    Require(asio::SelectBufferFrames(64, 512, 128, 0, 500) == 128, "fixed buffer size");
    Require(asio::SelectBufferFrames(64, 32, 64, 0, 64) == 0, "invalid driver bounds");
}

void Selection() {
    asio::Selection selected;
    Require(asio::ParseSelection(Config().endpointId, selected) && selected.left == 2 && selected.right == 3, "parse channels");
    Require(!asio::ParseSelection(selected.base + L"/1/1", selected), "duplicate channels accepted");
    Require(!asio::ParseSelection(selected.base + L"/-1/2", selected), "negative channel accepted");
    Require(!asio::ParseSelection(selected.base + L"/4096/2", selected), "out of range channel accepted");
    Require(!asio::ParseSelection(selected.base + L"/1/2extra", selected), "trailing data accepted");
    Require(!asio::ParseSelection(L"asio:not-a-guid", selected), "invalid CLSID accepted");
}

void PlaybackAndDrain() {
    DriverPlan scenario;
    plan = &scenario;
    AsioExclusiveSink sink(Factory);
    Source source;
    Require(SUCCEEDED(sink.Open(Config())), "open failed");
    Require(scenario.left == 2 && scenario.right == 3, "channel routing incorrect");
    Require(SUCCEEDED(sink.Start(source)), "start failed");
    WaitFor([&] { return source.fills.load() > 2; }, "no callbacks");
    sink.Pause();
    const auto pausedFills = source.fills.load();
    Sleep(25);
    Require(source.fills.load() == pausedFills, "pause consumed source");
    Require(SUCCEEDED(sink.Start(source)), "resume failed");
    WaitFor([&] { return source.fills.load() > pausedFills; }, "resume stuck");
    source.ready.store(false);
    WaitFor([&] { return sink.WaitingForSource(); }, "starvation not reported");
    const auto waitingFills = source.fills.load();
    Sleep(20);
    Require(source.fills.load() == waitingFills, "starvation consumed source");
    source.sealed.store(true);
    source.ready.store(true);
    WaitFor([&] { return sink.EndOfStreamComplete(); }, "tail did not drain");
    const auto stats = sink.Stats();
    Require(stats.clockPosition >= stats.clockTarget, "EOS completed without device clock");
    Require(stats.submittedFrames % 64 == 17, "tail frame count incorrect");
    Require(scenario.nonzeroPackets > 0, "driver buffers stayed silent");
    sink.Close();
    Require(scenario.releases == 1 && scenario.disposals == 1 && scenario.stops == 1, "driver teardown mismatch");
    Require(scenario.wrongThread == 0, "driver lifecycle moved apartments");
}

void NegotiationAndFailures() {
    DriverPlan scenario;
    plan = &scenario;
    AsioExclusiveSink sink(Factory);
    auto config = Config();
    scenario.rejectRate = true;
    config.format.sampleRate = 44100;
    Require(FAILED(sink.Open(config)), "strict rate unexpectedly converted");
    config.allowResampling = true;
    Require(SUCCEEDED(sink.Open(config)) && sink.Format().sampleRate == 48000, "rate conversion negotiation failed");
    sink.Close();
    scenario.type = 16;
    config = Config();
    Require(FAILED(sink.Open(config)), "strict precision unexpectedly reduced");
    config.allowResampling = true;
    Require(SUCCEEDED(sink.Open(config)) && sink.Format().validBits == 16, "bit depth negotiation failed");
    sink.Close();
    scenario.type = 32;
    Require(FAILED(sink.Open(config)), "DSD accepted");
    scenario.type = 18;
    config.endpointId = asio::DeviceBase(config.endpointId) + L"/7/8";
    Require(sink.Open(config) == E_INVALIDARG, "unavailable channel accepted");
    config = Config();
    scenario.failCreate = true;
    Require(FAILED(sink.Open(config)), "buffer failure ignored");
    scenario.failCreate = false;
    scenario.failStart = true;
    Require(SUCCEEDED(sink.Open(config)), "open after failure failed");
    Source source;
    Require(FAILED(sink.Start(source)), "start failure ignored");
    sink.Close();
    Require(scenario.instances == scenario.releases && scenario.wrongThread == 0, "failure cleanup leaked driver");
}

void NotificationsAndReopen() {
    DriverPlan scenario;
    plan = &scenario;
    AsioExclusiveSink sink(Factory);
    Source source;
    for (const long notification : {3L, 5L, 6L, 100L}) {
        scenario.notification = notification;
        Require(SUCCEEDED(sink.Open(Config())), "reset reopen failed");
        Require(SUCCEEDED(sink.Start(source)), "reset start failed");
        scenario.fireNotification.store(true);
        WaitFor([&] { return sink.State() == WasapiSinkState::Faulted; }, "driver notification did not fault");
        Require(sink.Stats().lastError == AUDCLNT_E_DEVICE_INVALIDATED, "wrong reset error");
        sink.Close();
    }
    scenario.notification = 100;
    Require(SUCCEEDED(sink.Open(Config())) && SUCCEEDED(sink.Start(source)), "paused rate setup failed");
    sink.Pause();
    scenario.fireNotification.store(true);
    WaitFor([&] { return sink.State() == WasapiSinkState::Faulted; }, "paused rate change ignored");
    sink.Close();
    Require(scenario.instances == scenario.releases && scenario.wrongThread == 0, "reset lifetime failure");
}

void ConcurrentOwnership() {
    DriverPlan scenario;
    plan = &scenario;
    AsioExclusiveSink first(Factory), second(Factory);
    Require(SUCCEEDED(first.Open(Config())), "first owner failed");
    Require(second.Open(Config()) == AUDCLNT_E_DEVICE_IN_USE, "second owner acquired driver");
    second.Close();
    Source source;
    Require(SUCCEEDED(first.Start(source)), "failed contender damaged first owner");
    WaitFor([&] { return source.fills.load() > 1; }, "first owner callback lost");
    first.Close();
    Require(SUCCEEDED(second.Open(Config())), "driver lease was not released");
    second.Close();
}

void CallbackWatchdog() {
    DriverPlan scenario;
    scenario.stall = true;
    plan = &scenario;
    AsioExclusiveSink sink(Factory);
    Source source;
    Require(SUCCEEDED(sink.Open(Config())) && SUCCEEDED(sink.Start(source)), "watchdog setup failed");
    WaitFor([&] { return sink.State() == WasapiSinkState::Faulted; }, "missing callbacks remained active");
    Require(sink.Stats().lastError == HRESULT_FROM_WIN32(ERROR_TIMEOUT), "watchdog error mismatch");
    sink.Close();
}

void FrozenClockCannotComplete() {
    DriverPlan scenario;
    scenario.freezeClock = true;
    plan = &scenario;
    AsioExclusiveSink sink(Factory);
    Source source;
    source.sealed.store(true);
    Require(SUCCEEDED(sink.Open(Config())) && SUCCEEDED(sink.Start(source)), "clock setup failed");
    WaitFor([&] { return sink.EndOfStreamSubmitted(); }, "EOF not submitted");
    Sleep(50);
    Require(!sink.EndOfStreamComplete(), "elapsed callbacks falsely certified drain");
    sink.Close();
}

void EmptyAndFullTerminalBuffers() {
    for (const std::uint32_t frames : {0u, 64u}) {
        DriverPlan scenario;
        plan = &scenario;
        AsioExclusiveSink sink(Factory);
        Source source;
        source.terminalFrames = frames;
        source.sealed.store(true);
        Require(SUCCEEDED(sink.Open(Config())) && SUCCEEDED(sink.Start(source)), "terminal setup failed");
        WaitFor([&] { return sink.EndOfStreamComplete(); }, "terminal buffer stuck");
        Require(source.fills == 1 && sink.Stats().submittedFrames == frames, "terminal source overread");
        sink.Close();
    }
}

}

int main() {
    const std::pair<const char*, void(*)()> cases[]{
        {"sample packing and buffer constraints", Packing},
        {"driver and channel selection", Selection},
        {"play pause starvation and clock drain", PlaybackAndDrain},
        {"format negotiation and failure cleanup", NegotiationAndFailures},
        {"reset resync rate latency notifications", NotificationsAndReopen},
        {"single owner and lease reuse", ConcurrentOwnership},
        {"callback watchdog", CallbackWatchdog},
        {"frozen clock cannot certify drain", FrozenClockCannotComplete},
        {"empty and full terminal packets", EmptyAndFullTerminalBuffers},
    };
    unsigned failures{};
    for (const auto& test : cases) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
