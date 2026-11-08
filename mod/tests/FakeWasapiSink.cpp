#include "audio_v2/WasapiExclusiveSink.h"

#include <mutex>

namespace {
std::mutex endpointMutex;
std::wstring openedEndpoint;
}

std::wstring LastOpenedTestEndpoint() {
    std::lock_guard lock(endpointMutex);
    return openedEndpoint;
}

namespace ammod::audio_v2 {

WasapiExclusiveSink::~WasapiExclusiveSink() { Close(); }

HRESULT WasapiExclusiveSink::Open(const WasapiSinkConfig& config) noexcept {
    config_ = config;
    bufferFrames_ = config.periodFrames;
    {
        std::lock_guard lock(endpointMutex);
        openedEndpoint = config.endpointId;
    }
    state_.store(WasapiSinkState::Open);
    return S_OK;
}

HRESULT WasapiExclusiveSink::Start(IIntegerPcmSource& source) noexcept {
    source_ = &source;
    state_.store(WasapiSinkState::Running);
    return S_OK;
}

void WasapiExclusiveSink::Pause() noexcept { state_.store(WasapiSinkState::Open); }
void WasapiExclusiveSink::Stop() noexcept { Pause(); }
void WasapiExclusiveSink::Close() noexcept {
    source_ = nullptr;
    state_.store(WasapiSinkState::Closed);
}
void WasapiExclusiveSink::SetResumeBlocked(bool blocked) noexcept { resumeBlocked_.store(blocked); }
WasapiSinkStats WasapiExclusiveSink::Stats() const noexcept { return {}; }

}
