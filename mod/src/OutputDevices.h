#pragma once

#include "AsioDevices.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <winrt/base.h>

#include <string>
#include <utility>
#include <vector>

namespace ammod::audio {

struct OutputDevice {
    std::wstring id;
    std::wstring name;
};

inline HRESULT EnumerateOutputDevices(std::vector<OutputDevice>& devices) {
    devices.clear();
    for (const auto& driver : ammod::asio::EnumerateDrivers())
        devices.push_back({driver.id, driver.name});
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return apartment;
    struct ApartmentScope {
        HRESULT result;
        ~ApartmentScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartmentScope{apartment};
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), enumerator.put_void());
    if (FAILED(result)) return devices.empty() ? result : S_OK;
    winrt::com_ptr<IMMDeviceCollection> collection;
    result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put());
    if (FAILED(result)) return devices.empty() ? result : S_OK;
    UINT count{};
    result = collection->GetCount(&count);
    if (FAILED(result)) return result;
    for (UINT index = 0; index < count; ++index) {
        winrt::com_ptr<IMMDevice> device;
        if (FAILED(collection->Item(index, device.put()))) continue;
        LPWSTR endpointId{};
        if (FAILED(device->GetId(&endpointId))) continue;
        OutputDevice entry{endpointId, endpointId};
        CoTaskMemFree(endpointId);
        winrt::com_ptr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
            PROPVARIANT name{};
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal) entry.name = name.pwszVal;
            PropVariantClear(&name);
        }
        entry.name = L"WASAPI · " + entry.name;
        devices.push_back(std::move(entry));
    }
    return S_OK;
}

}
