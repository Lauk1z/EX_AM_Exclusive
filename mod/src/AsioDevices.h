#pragma once

#include "audio_v2/AsioApi.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ammod::asio {

struct Selection {
    CLSID driver{};
    long left{}, right{1};
    std::wstring base;
};

inline bool IsAsio(std::wstring_view endpoint) noexcept {
    return endpoint.starts_with(L"asio:");
}

inline bool ParseSelection(std::wstring_view endpoint, Selection& selection) {
    if (!IsAsio(endpoint) || endpoint.size() < 43 || endpoint[5] != L'{' || endpoint[42] != L'}') return false;
    Selection parsed;
    parsed.base = endpoint.substr(0, 43);
    const auto identifier = parsed.base.substr(5);
    if (FAILED(CLSIDFromString(identifier.c_str(), &parsed.driver))) return false;
    if (endpoint.size() > 43) {
        std::size_t cursor = 43;
        const auto channel = [&](long& value) {
            if (cursor >= endpoint.size() || endpoint[cursor++] != L'/') return false;
            const auto start = cursor;
            value = 0;
            while (cursor < endpoint.size() && endpoint[cursor] >= L'0' && endpoint[cursor] <= L'9') {
                value = value * 10 + endpoint[cursor++] - L'0';
                if (value > 4095) return false;
            }
            return cursor != start;
        };
        if (!channel(parsed.left) || !channel(parsed.right) || cursor != endpoint.size() ||
            parsed.left == parsed.right) return false;
    }
    selection = std::move(parsed);
    return true;
}

inline std::wstring DeviceBase(const std::wstring& endpoint) {
    Selection selection;
    return ParseSelection(endpoint, selection) ? selection.base : endpoint;
}

struct RegisteredDriver { std::wstring id, name; };

inline std::vector<RegisteredDriver> EnumerateDrivers() {
    std::vector<RegisteredDriver> result;
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        HKEY drivers{};
        if (RegOpenKeyExW(root, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &drivers) != ERROR_SUCCESS) continue;
        for (DWORD index = 0;; ++index) {
            wchar_t name[256]{};
            DWORD length = 256;
            const auto status = RegEnumKeyExW(drivers, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) continue;
            wchar_t identifier[64]{}, description[256]{};
            DWORD bytes = sizeof(identifier);
            if (RegGetValueW(drivers, name, L"CLSID", RRF_RT_REG_SZ, nullptr, identifier, &bytes) != ERROR_SUCCESS) continue;
            CLSID driver{};
            if (FAILED(CLSIDFromString(identifier, &driver))) continue;
            wchar_t canonical[40]{};
            StringFromGUID2(driver, canonical, 40);
            const std::wstring registration = L"CLSID\\" + std::wstring(canonical) + L"\\InprocServer32";
            wchar_t server[32768]{};
            bytes = sizeof(server);
            if (RegGetValueW(HKEY_CLASSES_ROOT, registration.c_str(), nullptr,
                    RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_SUBKEY_WOW6464KEY,
                    nullptr, server, &bytes) != ERROR_SUCCESS) continue;
            bytes = sizeof(description);
            if (RegGetValueW(drivers, name, L"Description", RRF_RT_REG_SZ, nullptr,
                            description, &bytes) != ERROR_SUCCESS) wcscpy_s(description, name);
            const std::wstring id = L"asio:" + std::wstring(canonical);
            if (std::none_of(result.begin(), result.end(), [&](const auto& item) { return item.id == id; }))
                result.push_back({id, L"ASIO · " + std::wstring(description)});
        }
        RegCloseKey(drivers);
    }
    return result;
}

inline HANDLE AcquireDriverLease(const Selection& selection) {
    const auto name = L"Local\\AMExclusive.ASIO." + selection.base.substr(5);
    HANDLE lease = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!lease) return nullptr;
    const auto result = WaitForSingleObject(lease, 0);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return lease;
    CloseHandle(lease);
    return nullptr;
}

inline HRESULT ShowControlPanel(const std::wstring& endpoint) {
    Selection selection;
    if (!ParseSelection(endpoint, selection)) return E_INVALIDARG;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return apartment;
    HANDLE lease = AcquireDriverLease(selection);
    HRESULT result = HRESULT_FROM_WIN32(ERROR_BUSY);
    if (lease) {
        Driver* driver{};
        result = CreateDriver(selection.driver, &driver);
        if (SUCCEEDED(result)) {
            result = driver->init(GetDesktopWindow()) ? Result(driver->controlPanel()) : E_FAIL;
            driver->Release();
        }
        ReleaseMutex(lease);
        CloseHandle(lease);
    }
    CoUninitialize();
    return result;
}

}
