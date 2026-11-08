#pragma once

#include <windows.h>
#include <unknwn.h>
#include <cstdint>

namespace ammod::asio {

#pragma pack(push, 4)
struct Position { std::uint32_t high{}, low{}; };
struct ChannelInfo {
    long channel{}, input{}, active{}, group{}, sampleType{};
    char name[32]{};
};
struct BufferInfo {
    long input{}, channel{};
    void* buffers[2]{};
};
struct Time;
struct ClockSource;
struct Callbacks {
    void (*bufferSwitch)(long, long){};
    void (*sampleRateChanged)(double){};
    long (*message)(long, long, void*, double*){};
    Time* (*bufferSwitchTimeInfo)(Time*, long, long){};
};
#pragma pack(pop)

struct Driver : IUnknown {
    virtual long init(void* window) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* message) = 0;
    virtual long start() = 0;
    virtual long stop() = 0;
    virtual long getChannels(long* inputs, long* outputs) = 0;
    virtual long getLatencies(long* input, long* output) = 0;
    virtual long getBufferSize(long* minimum, long* maximum, long* preferred, long* granularity) = 0;
    virtual long canSampleRate(double rate) = 0;
    virtual long getSampleRate(double* rate) = 0;
    virtual long setSampleRate(double rate) = 0;
    virtual long getClockSources(ClockSource* clocks, long* count) = 0;
    virtual long setClockSource(long reference) = 0;
    virtual long getSamplePosition(Position* samples, Position* timestamp) = 0;
    virtual long getChannelInfo(ChannelInfo* channel) = 0;
    virtual long createBuffers(BufferInfo* buffers, long channels, long frames, Callbacks* callbacks) = 0;
    virtual long disposeBuffers() = 0;
    virtual long controlPanel() = 0;
    virtual long future(long selector, void* parameters) = 0;
    virtual long outputReady() = 0;
};

using DriverFactory = HRESULT (*)(const CLSID&, Driver**);

inline HRESULT CreateDriver(const CLSID& id, Driver** driver) {
    return CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, id,
                            reinterpret_cast<void**>(driver));
}

inline std::uint64_t SamplePosition(Position position) noexcept {
    return (static_cast<std::uint64_t>(position.high) << 32) | position.low;
}

inline HRESULT Result(long error) noexcept {
    if (error == 0 || error == 0x3f4847a0) return S_OK;
    if (error == -1000) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    if (error == -998) return E_INVALIDARG;
    if (error == -994) return E_OUTOFMEMORY;
    return E_FAIL;
}

static_assert(sizeof(long) == 4 && sizeof(void*) == 8);
static_assert(sizeof(Position) == 8 && sizeof(ChannelInfo) == 52);
static_assert(sizeof(BufferInfo) == 24 && sizeof(Callbacks) == 32);

}
