#pragma once
#include <string>
#include <system_error>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace posmv {
// Windows error text in UTF-8. MinGW's std::system_category() returns the ANSI code page,
// which GTK shows as garbage on Chinese Windows.
inline std::string win32_message(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text(buffer ? buffer : L"", buffer ? length : 0);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    std::string utf8(text.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr), '\0');
    if (!utf8.empty()) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    return (utf8.empty() ? std::string("Windows 錯誤") : utf8) + "（" + std::to_string(code) + "）";
}
// For std::system_error: code() keeps the Win32 error, what() is UTF-8.
inline const std::error_category& win32_category() {
    struct Category : std::error_category {
        const char* name() const noexcept override { return "win32"; }
        std::string message(int code) const override { return win32_message(static_cast<DWORD>(code)); }
    };
    static const Category category;
    return category;
}
}
