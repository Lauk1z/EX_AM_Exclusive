#include "audio_v2/AudioCoreRuntime.h"
#include "IpcProtocol.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ammod::audio_v2;

std::wstring LastOpenedTestEndpoint();

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate>
void WaitFor(Predicate predicate, const char* message) {
    const auto deadline = GetTickCount64() + 3000;
    while (!predicate() && GetTickCount64() < deadline) Sleep(5);
    Require(predicate(), message);
}

struct Playback {
    std::atomic<void*> activeConverter{};
    std::atomic<unsigned> faults{};
    AudioCoreRuntime runtime;
    int firstConverter{}, secondConverter{}, firstUnit{}, secondUnit{}, client{};
    ApplePcmFormatView source{0x616C6163u, 0, 0, 4096, 0, 2, 24, 48000};
    ApplePcmFormatView destination{kAppleLinearPcm, kAppleFormatFlagIsFloat, 8, 1, 8, 2, 32, 48000};

    Playback() {
        AudioCoreRuntimeCallbacks callbacks{};
        callbacks.context = this;
        callbacks.nativeHandoffPreservesScheduling = true;
        callbacks.prepareNativeHandoff = [](void*, void*) noexcept { return S_OK; };
        callbacks.onEventSnapshot = [](void* context, const AudioCoreRuntimeEventSnapshot& event) noexcept {
            auto& playback = *static_cast<Playback*>(context);
            if (event.event == AudioCoreRuntimeEvent::Active) playback.activeConverter.store(event.converter);
            if (event.event == AudioCoreRuntimeEvent::Faulted) ++playback.faults;
        };
        Require(runtime.Start(callbacks), "runtime start failed");
        runtime.SetUiEnabled(true);
    }

    ~Playback() { runtime.Stop(); }

    void Feed(void* converter) {
        std::vector<float> samples(4800 * 2, 0.25f);
        ApplePcmBufferView buffer{2, static_cast<std::uint32_t>(samples.size() * sizeof(float)), samples.data()};
        ApplePcmBufferListView list{1, &buffer};
        Require(runtime.OnPcmOutput(converter, destination, list, 4800), "PCM capture rejected");
    }

    void StartFirst() {
        runtime.OnConverterCreated(&firstConverter, source, destination, 1);
        runtime.OnAudioUnitInitialized(&firstUnit, &firstConverter, 48000, 2, 24, 1);
        runtime.OnNativeClientInitialized(&client, L"native-endpoint", AUDCLNT_SHAREMODE_SHARED, 0, nullptr, S_OK);
        runtime.OnNativeClientStarted(&client, S_OK);
        runtime.OnAudioUnitStarted(&firstUnit);
        Feed(&firstConverter);
        WaitFor([&] { return activeConverter.load() == &firstConverter; }, "first track did not activate");
    }

    void StageSecond() {
        runtime.OnConverterCreated(&secondConverter, source, destination, 2);
        runtime.OnAudioUnitInitialized(&secondUnit, &secondConverter, 48000, 2, 24, 2);
        runtime.OnNativeClientStarted(&client, S_OK);
        runtime.OnAudioUnitStarted(&secondUnit);
        Feed(&secondConverter);
    }

    void WaitForSecond() {
        WaitFor([&] { return activeConverter.load() == &secondConverter; }, "successor stuck behind retired track");
        Require(runtime.IsConverterBound(&secondConverter), "successor is not bound");
        Require(runtime.Stats().coordinator.capturedFrames == 4800, "successor PCM was lost or duplicated");
        Require(faults.load() == 0, "transition faulted");
    }
};

void DelayedSkipPreservesSuccessor() {
    Playback playback;
    playback.StartFirst();
    playback.runtime.OnTransportSkipArm(10);
    playback.runtime.OnNativeClientStopped(&playback.client, S_OK);
    playback.StageSecond();
    Require(playback.runtime.IsConverterBound(&playback.firstConverter), "old generation should await EOF");
    playback.runtime.OnTransportSkip(10);
    playback.WaitForSecond();
}

void ConfirmedSelectionSurvivesSlowGraph() {
    Playback playback;
    playback.StartFirst();
    playback.runtime.OnTransportSelectionArm(20);
    playback.runtime.OnTransportSkip(20);
    Sleep(800);
    playback.runtime.OnNativeClientStopped(&playback.client, S_OK);
    Require(!playback.runtime.IsConverterBound(&playback.firstConverter), "confirmed selection expired before stop");
    playback.StageSecond();
    playback.WaitForSecond();
}

void DelayedSelectionPreservesSuccessor() {
    Playback playback;
    playback.StartFirst();
    playback.runtime.OnTransportSelectionArm(30);
    playback.runtime.OnNativeClientStopped(&playback.client, S_OK);
    playback.StageSecond();
    Sleep(800);
    playback.runtime.OnTransportSkip(30);
    playback.WaitForSecond();
}

void StaleSkipDoesNotRetireSuccessor() {
    Playback playback;
    playback.StartFirst();
    playback.runtime.OnTransportSkipArm(40);
    playback.runtime.OnConverterDisposed(&playback.firstConverter);
    playback.StageSecond();
    playback.WaitForSecond();
    playback.runtime.OnTransportSkip(40);
    Require(playback.runtime.IsConverterBound(&playback.secondConverter), "stale skip retired successor");
}

void UnconfirmedSelectionKeepsNaturalDrain() {
    Playback playback;
    playback.StartFirst();
    playback.runtime.OnTransportSelectionArm(50);
    Sleep(800);
    playback.runtime.OnNativeClientStopped(&playback.client, S_OK);
    playback.StageSecond();
    Require(playback.runtime.IsConverterBound(&playback.firstConverter), "unconfirmed selection cancelled natural drain");
    playback.runtime.OnConverterDisposed(&playback.firstConverter);
    playback.WaitForSecond();
}

void DeviceSelectionAppliesToNextGeneration() {
    Playback playback;
    playback.runtime.SetOutputDevice(L"selected-dac");
    playback.StartFirst();
    Require(LastOpenedTestEndpoint() == L"selected-dac", "selected endpoint not used");
    playback.runtime.SetOutputDevice(L"");
    Require(LastOpenedTestEndpoint() == L"selected-dac", "device change disturbed live stream");
    playback.runtime.OnConverterDisposed(&playback.firstConverter);
    playback.StageSecond();
    playback.WaitForSecond();
    Require(LastOpenedTestEndpoint().empty(), "system-default selection not used");
}

void ProtocolRejectsUnterminatedDevice() {
    ammod::ipc::Message message{};
    message.type = ammod::ipc::MessageType::SetOutputDevice;
    message.role = ammod::ipc::Role::Ui;
    Require(ammod::ipc::IsValid(message), "valid device command rejected");
    message.selectedEndpointId[ammod::ipc::kEndpointIdChars - 1] = L'x';
    Require(!ammod::ipc::IsValid(message), "unterminated device command accepted");
}

void MissingAsioDoesNotFallBackAndCanRecover() {
    Playback playback;
    playback.runtime.SetOutputDevice(L"asio:{04CF8D95-E319-4C17-A6F3-A6FD6B49634F}/0/1");
    playback.runtime.OnConverterCreated(&playback.firstConverter, playback.source, playback.destination, 1);
    playback.runtime.OnAudioUnitInitialized(&playback.firstUnit, &playback.firstConverter, 48000, 2, 24, 1);
    playback.runtime.OnNativeClientInitialized(&playback.client, L"native", AUDCLNT_SHAREMODE_SHARED, 0, nullptr, S_OK);
    playback.runtime.OnNativeClientStarted(&playback.client, S_OK);
    playback.runtime.OnAudioUnitStarted(&playback.firstUnit);
    playback.Feed(&playback.firstConverter);
    WaitFor([&] { return playback.faults.load() != 0; }, "missing ASIO driver did not report failure");
    Require(playback.activeConverter.load() == nullptr, "missing ASIO fell back to WASAPI");
    playback.runtime.SetOutputDevice(L"");
    playback.runtime.SetUiEnabled(true);
    playback.faults.store(0);
    playback.StageSecond();
    playback.WaitForSecond();
}

}

int main() {
    const std::pair<const char*, void(*)()> cases[]{
        {"delayed skip", DelayedSkipPreservesSuccessor},
        {"slow selection", ConfirmedSelectionSurvivesSlowGraph},
        {"delayed selection", DelayedSelectionPreservesSuccessor},
        {"stale skip", StaleSkipDoesNotRetireSuccessor},
        {"unconfirmed selection", UnconfirmedSelectionKeepsNaturalDrain},
        {"device selection", DeviceSelectionAppliesToNextGeneration},
        {"device protocol", ProtocolRejectsUnterminatedDevice},
        {"ASIO missing driver and WASAPI recovery", MissingAsioDoesNotFallBackAndCanRecover},
    };
    unsigned failures{};
    for (const auto& test : cases) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
