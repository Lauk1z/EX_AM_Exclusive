#include "AsioExclusiveSink.h"
#include "ResamplingPolicy.h"
#include "../AsioDevices.h"
#include "../AudioErrors.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ammod::audio_v2 {
namespace {
SRWLOCK callbackLock = SRWLOCK_INIT;
AsioExclusiveSink* callbackOwner{};
}

AsioExclusiveSink::~AsioExclusiveSink() { Close(); }

HRESULT AsioExclusiveSink::Open(const WasapiSinkConfig& config) noexcept {
    Close();
    std::lock_guard lock(commandMutex_);
    try { config_ = config; } catch (...) { return E_OUTOFMEMORY; }
    error_.store(S_OK);
    format_ = config.format;
    commandEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    doneEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!commandEvent_ || !doneEvent_) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        if (commandEvent_) CloseHandle(commandEvent_);
        if (doneEvent_) CloseHandle(doneEvent_);
        commandEvent_ = doneEvent_ = nullptr;
        return result;
    }
    worker_ = CreateThread(nullptr, 0, WorkerThunk, this, 0, nullptr);
    if (!worker_) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(commandEvent_);
        CloseHandle(doneEvent_);
        commandEvent_ = doneEvent_ = nullptr;
        return result;
    }
    return Invoke(Command::Open);
}

HRESULT AsioExclusiveSink::Invoke(Command command) noexcept {
    if (!worker_) return E_UNEXPECTED;
    command_ = command;
    SetEvent(commandEvent_);
    const HANDLE handles[]{doneEvent_, worker_};
    return WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0
        ? commandResult_ : E_UNEXPECTED;
}

HRESULT AsioExclusiveSink::Start(IIntegerPcmSource& source) noexcept {
    std::lock_guard lock(commandMutex_);
    if (state_.load() != WasapiSinkState::Open) return E_UNEXPECTED;
    source_ = &source;
    return Invoke(Command::Start);
}

void AsioExclusiveSink::Pause() noexcept {
    std::lock_guard lock(commandMutex_);
    if (worker_) (void)Invoke(Command::Pause);
}

void AsioExclusiveSink::Stop() noexcept {
    std::lock_guard lock(commandMutex_);
    if (worker_) (void)Invoke(Command::Stop);
}

void AsioExclusiveSink::Close() noexcept {
    std::lock_guard lock(commandMutex_);
    if (!worker_) return;
    (void)Invoke(Command::Close);
    WaitForSingleObject(worker_, INFINITE);
    CloseHandle(worker_);
    CloseHandle(commandEvent_);
    CloseHandle(doneEvent_);
    worker_ = commandEvent_ = doneEvent_ = nullptr;
    source_ = nullptr;
    state_.store(WasapiSinkState::Closed);
}

DWORD WINAPI AsioExclusiveSink::WorkerThunk(void* context) noexcept {
    return static_cast<AsioExclusiveSink*>(context)->WorkerMain();
}

DWORD AsioExclusiveSink::WorkerMain() noexcept {
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool closing{};
    while (!closing) {
        const auto wait = MsgWaitForMultipleObjects(1, &commandEvent_, FALSE, 20, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            commandResult_ = S_OK;
            switch (command_) {
            case Command::Open:
                try { commandResult_ = FAILED(apartment) ? apartment : OpenDriver(); }
                catch (...) { commandResult_ = E_OUTOFMEMORY; }
                if (FAILED(commandResult_)) { CloseDriver(); Fault(commandResult_); }
                break;
            case Command::Start: commandResult_ = StartDriver(); break;
            case Command::Pause:
                accepting_.store(false);
                while (callbackDepth_.load() != 0) SwitchToThread();
                if (state_.load() == WasapiSinkState::Running) state_.store(WasapiSinkState::Open);
                break;
            case Command::Stop: StopDriver(); break;
            case Command::Close: CloseDriver(); closing = true; break;
            }
            SetEvent(doneEvent_);
        } else if (wait == WAIT_OBJECT_0 + 1) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else if (wait == WAIT_FAILED) {
            Fault(HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        if (driverRunning_) {
            const auto now = GetTickCount64();
            const auto periodMs = (static_cast<std::uint64_t>(bufferFrames_) * 1000) / format_.sampleRate;
            if (now - lastCallbackTick_.load() > std::max<std::uint64_t>(2000, periodMs * 8))
                Fault(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            if (state_.load() == WasapiSinkState::Running && terminal_.load() && !complete_.load() &&
                now - drainStartTick_.load() > 5000 + periodMs * 4)
                Fault(ammod::audio::kEndpointDrainTimeout);
            if (FAILED(error_.load())) StopDriver();
        }
    }
    CloseDriver();
    if (SUCCEEDED(apartment)) CoUninitialize();
    return 0;
}

HRESULT AsioExclusiveSink::OpenDriver() {
    asio::Selection selection;
    if (!IsValidFormat(config_.format) || !asio::ParseSelection(config_.endpointId, selection)) return E_INVALIDARG;
    driverLease_ = asio::AcquireDriverLease(selection);
    if (!driverLease_) return AUDCLNT_E_DEVICE_IN_USE;
    AcquireSRWLockExclusive(&callbackLock);
    const bool available = callbackOwner == nullptr;
    if (available) callbackOwner = this;
    ReleaseSRWLockExclusive(&callbackLock);
    if (!available) return AUDCLNT_E_DEVICE_IN_USE;
    HRESULT result = factory_(selection.driver, &driver_);
    if (FAILED(result)) return result;
    if (!driver_ || !driver_->init(GetDesktopWindow())) return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    long inputs{}, outputs{};
    result = asio::Result(driver_->getChannels(&inputs, &outputs));
    if (FAILED(result)) return result;
    if (selection.left >= outputs || selection.right >= outputs) return E_INVALIDARG;
    const long channels[]{selection.left, selection.right};
    double currentRate{};
    (void)driver_->getSampleRate(&currentRate);
    const auto hint = std::isfinite(currentRate) && currentRate >= 8000 && currentRate <= 768000
        ? static_cast<std::uint32_t>(currentRate) : 0;
    const auto rates = SelectOutputSampleRates(config_.format.sampleRate, config_.allowResampling, hint);
    bool rateSelected{};
    for (std::size_t index = 0; index < rates.count; ++index) {
        const double requested = rates.values[index];
        if (driver_->canSampleRate(requested) != 0 || driver_->setSampleRate(requested) != 0) continue;
        double actual{};
        if (driver_->getSampleRate(&actual) != 0 || !std::isfinite(actual) || std::abs(actual - requested) > 0.01) continue;
        format_.sampleRate = rates.values[index];
        rateSelected = true;
        break;
    }
    if (!rateSelected) return config_.allowResampling ? ammod::audio::kExclusiveFormatUnavailable
                                                      : ammod::audio::kBitPerfectFormatUnavailable;
    unsigned precision = 32;
    for (unsigned channel = 0; channel < 2; ++channel) {
        asio::ChannelInfo info{};
        info.channel = channels[channel];
        result = asio::Result(driver_->getChannelInfo(&info));
        if (FAILED(result)) return result;
        layouts_[channel] = asio::Layout(info.sampleType);
        if (!layouts_[channel].bytes) return AUDCLNT_E_UNSUPPORTED_FORMAT;
        precision = std::min(precision, layouts_[channel].bits);
        buffers_[channel] = {};
        buffers_[channel].channel = channels[channel];
    }
    const auto sourceBits = EffectiveSourceValidBits(config_.format);
    if (precision < sourceBits && !config_.allowResampling) return ammod::audio::kBitPerfectFormatUnavailable;
    format_.validBits = static_cast<std::uint16_t>(precision);
    format_.containerBits = 32;
    format_.sourceValidBits = sourceBits;
    long minimum{}, maximum{}, preferred{}, granularity{};
    result = asio::Result(driver_->getBufferSize(&minimum, &maximum, &preferred, &granularity));
    if (FAILED(result)) return result;
    const auto requested = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(config_.periodFrames) * format_.sampleRate / config_.format.sampleRate);
    const long frames = asio::SelectBufferFrames(minimum, maximum, preferred, granularity, requested);
    if (!frames || frames > 65536) return AUDCLNT_E_BUFFER_SIZE_ERROR;
    bufferFrames_ = static_cast<std::uint32_t>(frames);
    try { canonical_.assign(static_cast<std::size_t>(bufferFrames_) * 2, 0); }
    catch (...) { return E_OUTOFMEMORY; }
    error_.store(S_OK);
    accepting_.store(false);
    waiting_.store(false);
    resumeBlocked_.store(false);
    terminal_.store(false);
    complete_.store(false);
    clockPosition_.store(0);
    clockTarget_.store(0);
    clockSeen_ = false;
    submittedFrames_.store(0);
    submittedBuffers_.store(0);
    silenceFrames_.store(0);
    sourceWaits_.store(0);
    starts_.store(0);
    result = asio::Result(driver_->createBuffers(buffers_, 2, frames, &callbacks_));
    if (FAILED(result)) return result;
    buffersCreated_ = true;
    for (unsigned channel = 0; channel < 2; ++channel) {
        asio::ChannelInfo info{};
        info.channel = channels[channel];
        if (driver_->getChannelInfo(&info) != 0) return AUDCLNT_E_UNSUPPORTED_FORMAT;
        const auto layout = asio::Layout(info.sampleType);
        const auto expected = layouts_[channel];
        if (layout.bytes != expected.bytes || layout.bits != expected.bits ||
            layout.floating != expected.floating || layout.bigEndian != expected.bigEndian ||
            layout.rightAligned != expected.rightAligned) return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    for (const auto& buffer : buffers_)
        if (!buffer.buffers[0] || !buffer.buffers[1]) return E_POINTER;
    long inputLatency{}, outputLatency{};
    result = asio::Result(driver_->getLatencies(&inputLatency, &outputLatency));
    if (FAILED(result) || outputLatency < 0 || outputLatency > static_cast<long>(format_.sampleRate * 10))
        return FAILED(result) ? result : E_INVALIDARG;
    outputLatency_ = static_cast<std::uint32_t>(outputLatency);
    Silence(0);
    Silence(1);
    outputReady_ = driver_->outputReady() == 0;
    if (FAILED(error_.load())) return error_.load();
    state_.store(WasapiSinkState::Open);
    return S_OK;
}

HRESULT AsioExclusiveSink::StartDriver() noexcept {
    if (!driver_ || !buffersCreated_ || !source_) return E_UNEXPECTED;
    if (FAILED(error_.load())) return error_.load();
    state_.store(WasapiSinkState::Running);
    if (terminal_.load()) drainStartTick_.store(GetTickCount64());
    lastClockAdvanceTick_ = GetTickCount64();
    accepting_.store(true);
    if (driverRunning_) return S_OK;
    lastCallbackTick_.store(GetTickCount64());
    const HRESULT result = asio::Result(driver_->start());
    if (FAILED(result)) {
        accepting_.store(false);
        (void)driver_->stop();
        Fault(result);
        return result;
    }
    driverRunning_ = true;
    starts_.fetch_add(1);
    return S_OK;
}

void AsioExclusiveSink::StopDriver() noexcept {
    accepting_.store(false);
    if (driver_ && driverRunning_) {
        const auto result = asio::Result(driver_->stop());
        if (FAILED(result)) Fault(result);
        driverRunning_ = false;
    }
    while (callbackDepth_.load() != 0) SwitchToThread();
    if (state_.load() != WasapiSinkState::Faulted && state_.load() != WasapiSinkState::Closed)
        state_.store(WasapiSinkState::Open);
}

void AsioExclusiveSink::CloseDriver() noexcept {
    StopDriver();
    if (driver_ && buffersCreated_) {
        (void)driver_->disposeBuffers();
        buffersCreated_ = false;
    }
    AcquireSRWLockExclusive(&callbackLock);
    if (callbackOwner == this) callbackOwner = nullptr;
    ReleaseSRWLockExclusive(&callbackLock);
    if (driver_) { driver_->Release(); driver_ = nullptr; }
    if (driverLease_) {
        ReleaseMutex(driverLease_);
        CloseHandle(driverLease_);
        driverLease_ = nullptr;
    }
    canonical_.clear();
    state_.store(WasapiSinkState::Closed);
}

void AsioExclusiveSink::Fault(HRESULT error) noexcept {
    HRESULT expected = S_OK;
    error_.compare_exchange_strong(expected, error);
    accepting_.store(false);
    state_.store(WasapiSinkState::Faulted);
}

void AsioExclusiveSink::Silence(long index) noexcept {
    if (index < 0 || index > 1) return;
    for (unsigned channel = 0; channel < 2; ++channel)
        if (buffers_[channel].buffers[index])
            std::memset(buffers_[channel].buffers[index], 0,
                        static_cast<std::size_t>(bufferFrames_) * layouts_[channel].bytes);
}

void AsioExclusiveSink::Render(long index) noexcept {
    if (index < 0 || index > 1) { Fault(E_INVALIDARG); return; }
    lastCallbackTick_.store(GetTickCount64());
    Silence(index);
    if (!accepting_.load() || resumeBlocked_.load() || complete_.load()) return;
    asio::Position samples{}, timestamp{};
    const auto clockResult = asio::Result(driver_->getSamplePosition(&samples, &timestamp));
    if (FAILED(clockResult)) { Fault(clockResult); return; }
    const auto position = asio::SamplePosition(samples);
    if (clockSeen_ && position < clockPosition_.load()) { Fault(AUDCLNT_E_DEVICE_INVALIDATED); return; }
    const auto now = GetTickCount64();
    if (!clockSeen_ || position > clockPosition_.load()) lastClockAdvanceTick_ = now;
    const auto periodMs = (static_cast<std::uint64_t>(bufferFrames_) * 1000) / format_.sampleRate;
    if (now - lastClockAdvanceTick_ > std::max<std::uint64_t>(2000, periodMs * 8)) {
        Fault(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return;
    }
    clockSeen_ = true;
    clockPosition_.store(position);
    if (terminal_.load()) {
        if (position >= clockTarget_.load()) complete_.store(true);
        return;
    }
    const auto prepared = source_->Prepare(bufferFrames_);
    if (prepared == kAudioSourceWouldBlock) {
        waiting_.store(true);
        sourceWaits_.fetch_add(1);
        silenceFrames_.fetch_add(bufferFrames_);
        return;
    }
    waiting_.store(false);
    if (FAILED(prepared)) { Fault(prepared); return; }
    std::uint32_t written{};
    bool end = prepared == S_FALSE;
    if (!end) {
        const auto result = source_->Fill(canonical_.data(), bufferFrames_, written, end);
        if (FAILED(result) || written > bufferFrames_ || (written < bufferFrames_ && !end)) {
            Fault(FAILED(result) ? result : AUDCLNT_E_BUFFER_ERROR);
            return;
        }
    }
    if (written != 0) {
        const std::uint64_t tail = static_cast<std::uint64_t>(bufferFrames_) * 2 + outputLatency_;
        if (position > UINT64_MAX - tail) { Fault(E_INVALIDARG); return; }
        for (unsigned channel = 0; channel < 2; ++channel)
            asio::PackChannel(canonical_.data(), buffers_[channel].buffers[index], written, channel, layouts_[channel]);
        submittedFrames_.fetch_add(written);
        submittedBuffers_.fetch_add(1);
        clockTarget_.store(position + tail);
        silenceFrames_.fetch_add(bufferFrames_ - written);
    }
    if (end) {
        drainStartTick_.store(GetTickCount64());
        terminal_.store(true);
        if (submittedFrames_.load() == 0 || position >= clockTarget_.load()) complete_.store(true);
    }
}

void AsioExclusiveSink::BufferSwitch(long index, long) noexcept {
    if (!TryAcquireSRWLockShared(&callbackLock)) return;
    auto* sink = callbackOwner;
    if (sink) {
        sink->callbackDepth_.fetch_add(1);
        if (!sink->rendering_.test_and_set()) {
            sink->Render(index);
            if (sink->outputReady_) {
                const auto result = asio::Result(sink->driver_->outputReady());
                if (FAILED(result)) sink->Fault(result);
            }
            sink->rendering_.clear();
        } else {
            sink->Fault(AUDCLNT_E_BUFFER_ERROR);
        }
        sink->callbackDepth_.fetch_sub(1);
    }
    ReleaseSRWLockShared(&callbackLock);
}

void AsioExclusiveSink::SampleRateChanged(double rate) noexcept {
    if (!TryAcquireSRWLockShared(&callbackLock)) return;
    if (callbackOwner &&
        (callbackOwner->state_.load() == WasapiSinkState::Open ||
         callbackOwner->state_.load() == WasapiSinkState::Running) &&
        (!std::isfinite(rate) || std::abs(rate - callbackOwner->format_.sampleRate) > 0.01))
        callbackOwner->Fault(AUDCLNT_E_DEVICE_INVALIDATED);
    ReleaseSRWLockShared(&callbackLock);
}

long AsioExclusiveSink::DriverMessage(long selector, long value, void*, double*) noexcept {
    if (selector == 1) return value == 2 || value == 3 || value == 5 || value == 6;
    if (selector == 2) return 2;
    if (selector != 3 && selector != 5 && selector != 6) return 0;
    if (!TryAcquireSRWLockShared(&callbackLock)) return 0;
    if (callbackOwner) callbackOwner->Fault(AUDCLNT_E_DEVICE_INVALIDATED);
    ReleaseSRWLockShared(&callbackLock);
    return 1;
}

asio::Time* AsioExclusiveSink::BufferSwitchTime(asio::Time* time, long index, long direct) noexcept {
    BufferSwitch(index, direct);
    return time;
}

WasapiSinkStats AsioExclusiveSink::Stats() const noexcept {
    WasapiSinkStats stats{};
    stats.submittedFrames = submittedFrames_.load();
    stats.submittedBuffers = submittedBuffers_.load();
    stats.controlledSilenceFrames = silenceFrames_.load();
    stats.sourceWaits = sourceWaits_.load();
    stats.lastError = error_.load();
    stats.clockPosition = clockPosition_.load();
    stats.clockTarget = clockTarget_.load();
    stats.clockFrequency = format_.sampleRate;
    stats.clientStarts = starts_.load();
    stats.endOfStreamSubmitted = terminal_.load();
    stats.endOfStreamComplete = complete_.load();
    return stats;
}

}
