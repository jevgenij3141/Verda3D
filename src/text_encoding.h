#pragma once

#include <windows.h>
#include <string>

inline std::wstring widenUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    std::wstring text(size, L'\0');
    if (size) MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), text.data(), size);
    return text;
}

inline std::string narrowUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string text(size, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()),
                                  text.data(), size, nullptr, nullptr);
    return text;
}
