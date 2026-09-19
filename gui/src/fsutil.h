#pragma once
// ============================================================================
//  fsutil.h —— GUI 的文件读写统一走宽字符 API
//
//  为什么需要它：GUI 内部路径一律是 UTF-8（从 GetOpenFileNameW 转来的）。
//  直接把这些字节交给 std::ifstream / std::ofstream / fopen，MSVC 会按 **ANSI**
//  代码页解释 —— 路径里只要有中文（例如桌面的「怪猎配置」文件夹、中文用户目录），
//  就变成“文件不存在”，界面报「读取失败」，写盘也一样失败。
//  这里统一转成宽路径再调用 Win32，中文路径就正常了。
// ============================================================================
#include <windows.h>
#include <string>

inline std::wstring FsWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    return w;
}

inline std::string FsUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// 长路径：CreateFileW 默认走 MAX_PATH(260) 限制，mod 管理器/深层压缩包解出来的
// 路径很容易超；加 \\?\ 前缀就能突破（要求绝对路径、只用反斜杠）。
inline std::wstring FsLongPath(const std::string& utf8) {
    std::wstring w = FsWide(utf8);
    for (auto& c : w) if (c == L'/') c = L'\\';
    if (w.size() < 250) return w;                     // 普通长度直接用
    if (w.rfind(L"\\\\", 0) == 0)                     // UNC：\\server\share\...
        return L"\\\\?\\UNC\\" + w.substr(2);
    if (w.size() >= 2 && w[1] == L':')                // 盘符绝对路径
        return L"\\\\?\\" + w;
    return w;                                         // 相对路径保持原样
}

// 读整个文件（UTF-8 路径）。文件不存在/打不开返回 false。
inline bool FsRead(const std::string& utf8Path, std::string& out) {
    out.clear();
    HANDLE h = ::CreateFileW(FsLongPath(utf8Path).c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (64LL << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out.resize(static_cast<std::size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = TRUE;
    if (!out.empty())
        ok = ::ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &rd, nullptr);
    ::CloseHandle(h);
    if (!ok) { out.clear(); return false; }
    out.resize(rd);
    return true;
}

// 写整个文件（append=true 追加）。UTF-8 路径。
inline bool FsWrite(const std::string& utf8Path, const std::string& data, bool append = false) {
    HANDLE h = ::CreateFileW(FsLongPath(utf8Path).c_str(),
                             append ? FILE_APPEND_DATA : GENERIC_WRITE,
                             FILE_SHARE_READ, nullptr,
                             append ? OPEN_ALWAYS : CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (!data.empty()) {
        DWORD wr = 0;
        ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &wr, nullptr) &&
             wr == static_cast<DWORD>(data.size());
    }
    ::CloseHandle(h);
    return ok;
}

inline bool FsExists(const std::string& utf8Path) {
    const DWORD a = ::GetFileAttributesW(FsLongPath(utf8Path).c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

inline bool FsCopy(const std::string& src, const std::string& dst, bool failIfExists = false) {
    return ::CopyFileW(FsLongPath(src).c_str(), FsLongPath(dst).c_str(),
                       failIfExists ? TRUE : FALSE) != FALSE;
}
