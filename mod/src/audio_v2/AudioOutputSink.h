#pragma once

#include "AsioExclusiveSink.h"
#include "../AsioDevices.h"

namespace ammod::audio_v2 {

class AudioOutputSink final {
public:
    HRESULT Open(const WasapiSinkConfig& config) noexcept {
        Close();
        asioSelected_ = asio::IsAsio(config.endpointId);
        return asioSelected_ ? asio_.Open(config) : wasapi_.Open(config);
    }
    HRESULT Start(IIntegerPcmSource& source) noexcept { return asioSelected_ ? asio_.Start(source) : wasapi_.Start(source); }
    void Pause() noexcept { if (asioSelected_) asio_.Pause(); else wasapi_.Pause(); }
    void Stop() noexcept { if (asioSelected_) asio_.Stop(); else wasapi_.Stop(); }
    void Close() noexcept { asio_.Close(); wasapi_.Close(); }
    WasapiSinkState State() const noexcept { return asioSelected_ ? asio_.State() : wasapi_.State(); }
    WasapiSinkStats Stats() const noexcept { return asioSelected_ ? asio_.Stats() : wasapi_.Stats(); }
    const PcmFormat& Format() const noexcept { return asioSelected_ ? asio_.Format() : wasapi_.Format(); }
    std::uint32_t BufferFrames() const noexcept { return asioSelected_ ? asio_.BufferFrames() : wasapi_.BufferFrames(); }
    bool WaitingForSource() const noexcept { return asioSelected_ ? asio_.WaitingForSource() : wasapi_.WaitingForSource(); }
    bool EndOfStreamSubmitted() const noexcept { return asioSelected_ ? asio_.EndOfStreamSubmitted() : wasapi_.EndOfStreamSubmitted(); }
    bool EndOfStreamComplete() const noexcept { return asioSelected_ ? asio_.EndOfStreamComplete() : wasapi_.EndOfStreamComplete(); }
    void SetResumeBlocked(bool blocked) noexcept {
        if (asioSelected_) asio_.SetResumeBlocked(blocked); else wasapi_.SetResumeBlocked(blocked);
    }
    bool IsAsio() const noexcept { return asioSelected_; }

private:
    bool asioSelected_{};
    AsioExclusiveSink asio_;
    WasapiExclusiveSink wasapi_;
};

}
