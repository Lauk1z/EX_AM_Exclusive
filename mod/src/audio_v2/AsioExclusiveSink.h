#pragma once

#include "AsioApi.h"
#include "AsioSampleConversion.h"
#include "WasapiExclusiveSink.h"
#include <mutex>

namespace ammod::audio_v2 {

class AsioExclusiveSink final {
public:
    explicit AsioExclusiveSink(asio::DriverFactory factory = asio::CreateDriver) noexcept : factory_(factory) {}
    ~AsioExclusiveSink();
    AsioExclusiveSink(const AsioExclusiveSink&) = delete;
    AsioExclusiveSink& operator=(const AsioExclusiveSink&) = delete;
    HRESULT Open(const WasapiSinkConfig& config) noexcept;
    HRESULT Start(IIntegerPcmSource& source) noexcept;
    void Pause() noexcept;
    void Stop() noexcept;
    void Close() noexcept;
    WasapiSinkState State() const noexcept { return state_.load(); }
    WasapiSinkStats Stats() const noexcept;
    const PcmFormat& Format() const noexcept { return format_; }
    std::uint32_t BufferFrames() const noexcept { return bufferFrames_; }
    bool WaitingForSource() const noexcept { return waiting_.load(); }
    bool EndOfStreamSubmitted() const noexcept { return terminal_.load(); }
    bool EndOfStreamComplete() const noexcept { return complete_.load(); }
    void SetResumeBlocked(bool blocked) noexcept { resumeBlocked_.store(blocked); }

private:
    enum class Command { Open, Start, Pause, Stop, Close };
    HRESULT Invoke(Command command) noexcept;
    static DWORD WINAPI WorkerThunk(void* context) noexcept;
    DWORD WorkerMain() noexcept;
    HRESULT OpenDriver();
    HRESULT StartDriver() noexcept;
    void StopDriver() noexcept;
    void CloseDriver() noexcept;
    void Render(long index) noexcept;
    void Silence(long index) noexcept;
    void Fault(HRESULT error) noexcept;
    static void BufferSwitch(long index, long direct) noexcept;
    static void SampleRateChanged(double rate) noexcept;
    static long DriverMessage(long selector, long value, void*, double*) noexcept;
    static asio::Time* BufferSwitchTime(asio::Time* time, long index, long direct) noexcept;

    asio::DriverFactory factory_;
    std::mutex commandMutex_;
    HANDLE commandEvent_{}, doneEvent_{}, worker_{};
    Command command_{Command::Close};
    HRESULT commandResult_{S_OK};
    WasapiSinkConfig config_{};
    PcmFormat format_{};
    IIntegerPcmSource* source_{};
    asio::Driver* driver_{};
    HANDLE driverLease_{};
    bool buffersCreated_{}, driverRunning_{}, outputReady_{};
    asio::BufferInfo buffers_[2]{};
    asio::SampleLayout layouts_[2]{};
    asio::Callbacks callbacks_{BufferSwitch, SampleRateChanged, DriverMessage, BufferSwitchTime};
    std::uint32_t bufferFrames_{}, outputLatency_{};
    std::vector<std::int32_t> canonical_;
    std::atomic<WasapiSinkState> state_{WasapiSinkState::Closed};
    std::atomic<bool> accepting_{}, waiting_{}, resumeBlocked_{}, terminal_{}, complete_{};
    std::atomic_flag rendering_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> callbackDepth_{};
    std::atomic<HRESULT> error_{S_OK};
    std::atomic<ULONGLONG> lastCallbackTick_{}, drainStartTick_{};
    ULONGLONG lastClockAdvanceTick_{};
    std::atomic<std::uint64_t> submittedFrames_{}, submittedBuffers_{}, silenceFrames_{}, sourceWaits_{}, starts_{};
    std::atomic<std::uint64_t> clockPosition_{}, clockTarget_{};
    bool clockSeen_{};
};

}
