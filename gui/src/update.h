// 更新检查（自包含，无第三方依赖）
//  - GitHub Releases API 取最新版本 tag / 下载地址 / 更新说明
//  - 版本号比较
//  - 下载文件（支持 HTTP 代理）
// 说明：只用 WinHTTP，宽字符路径，避免中文路径问题。
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace wseupd {

struct Latest {
    bool        ok = false;      // 是否成功取到信息
    std::string tag;             // 如 "v2.25"
    std::string zipUrl;          // 组合包下载直链
    std::string notes;           // 更新说明（release body 的前若干行）
    std::string err;             // 失败原因（ok=false 时）
};

// ---------- 小工具 ----------
inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 版本号比较：把 "v2.10.3" → {2,10,3}；返回 a>b / a==b / a<b
inline std::vector<int> ParseVer(const std::string& s) {
    std::vector<int> v;
    int cur = 0;
    bool has = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); has = true; }
        else if (c == '.') { if (has) v.push_back(cur); cur = 0; has = false; }
        else if (has) { v.push_back(cur); cur = 0; has = false; }
    }
    if (has) v.push_back(cur);
    return v;
}

inline int CompareVer(const std::string& a, const std::string& b) {
    std::vector<int> x = ParseVer(a), y = ParseVer(b);
    const size_t n = (x.size() > y.size()) ? x.size() : y.size();
    for (size_t i = 0; i < n; ++i) {
        const int xi = (i < x.size()) ? x[i] : 0;
        const int yi = (i < y.size()) ? y[i] : 0;
        if (xi != yi) return (xi > yi) ? 1 : -1;
    }
    return 0;
}

// ---------- HTTP ----------
struct Http {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;

    bool Open(const std::wstring& host, const std::string& proxyUtf8, std::string& err, bool https) {
        const std::wstring proxy = Widen(proxyUtf8);
        session = WinHttpOpen(L"WeaponSoundEnhance-GUI/1.0",
                              proxy.empty() ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
                                            : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                              proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxy.c_str(),
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { err = "WinHttpOpen 失败"; return false; }
        WinHttpSetTimeouts(session, 6000, 6000, 12000, 20000);
        connect = WinHttpConnect(session, host.c_str(), https ? INTERNET_DEFAULT_HTTPS_PORT
                                                              : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!connect) { err = "连接服务器失败（网络或代理设置）"; return false; }
        return true;
    }

    ~Http() {
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
};

// GET path（如 L"/repos/x/y/releases/latest"）→ 响应体（字符串）
inline bool HttpGetString(const std::wstring& host, const std::wstring& path,
                          const std::string& proxyUtf8, bool https,
                          std::string& out, std::string& err, std::wstring* finalUrl = nullptr) {
    Http h;
    if (!h.Open(host, proxyUtf8, err, https)) return false;

    HINTERNET req = WinHttpOpenRequest(h.connect, L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { err = "创建请求失败"; return false; }
    // GitHub API 必须带 User-Agent
    WinHttpAddRequestHeaders(req, L"User-Agent: WeaponSoundEnhance-GUI\r\nAccept: application/vnd.github+json\r\n",
                             (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        if (finalUrl) {
            wchar_t urlBuf[2048] = {};
            DWORD ul = sizeof(urlBuf);
            if (WinHttpQueryOption(req, WINHTTP_OPTION_URL, urlBuf, &ul)) *finalUrl = urlBuf;
        }
        if (status == 200) {
            out.clear();
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &got) || got == 0) break;
                out.append(chunk.data(), got);
                if (out.size() > (8u << 20)) break;   // 上限 8MB
            }
            ok = true;
        } else {
            err = "服务器返回 " + std::to_string((int)status);
        }
    } else {
        err = "请求失败（网络不通？可试试在 ini 里设 UpdateProxy）";
    }
    WinHttpCloseHandle(req);
    return ok;
}

// GET → 写入文件（宽路径）
inline bool HttpGetFile(const std::wstring& host, const std::wstring& path,
                        const std::string& proxyUtf8, bool https,
                        const std::wstring& dstPath, std::string& err,
                        void (*progress)(std::size_t, std::size_t) = nullptr) {
    Http h;
    if (!h.Open(host, proxyUtf8, err, https)) return false;
    HINTERNET req = WinHttpOpenRequest(h.connect, L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { err = "创建请求失败"; return false; }
    WinHttpAddRequestHeaders(req, L"User-Agent: WeaponSoundEnhance-GUI\r\n", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    HANDLE f = INVALID_HANDLE_VALUE;
    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        DWORD total = 0;
        {
            wchar_t cl[64] = {};
            DWORD cln = sizeof(cl);
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                    cl, &cln, WINHTTP_NO_HEADER_INDEX))
                total = (DWORD)_wtoi(cl);
        }
        if (status == 200) {
            f = CreateFileW(dstPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) {
                err = "无法写入文件（可能被占用）";
            } else {
                std::size_t written = 0;
                ok = true;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                    std::vector<char> buf(avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(req, buf.data(), avail, &got) || got == 0) break;
                    DWORD wr = 0;
                    if (!WriteFile(f, buf.data(), got, &wr, nullptr) || wr != got) { ok = false; err = "写文件失败"; break; }
                    written += got;
                    if (progress) progress(written, total);
                }
            }
        } else {
            err = "下载失败，服务器返回 " + std::to_string((int)status);
        }
    } else {
        err = "下载请求失败（网络不通？可试试在 ini 里设 UpdateProxy）";
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    WinHttpCloseHandle(req);
    return ok;
}

// ---------- GitHub Release 解析 ----------
inline std::string JsonStr(const std::string& j, const std::string& key, size_t from = 0) {
    const std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat, from);
    if (p == std::string::npos) return std::string();
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return std::string();
    p = j.find('"', p);
    if (p == std::string::npos) return std::string();
    std::string out;
    for (size_t i = p + 1; i < j.size(); ++i) {
        const char c = j[i];
        if (c == '\\' && i + 1 < j.size()) {
            const char n = j[++i];
            switch (n) {
            case 'n': out += '\n'; break;
            case 'r': break;
            case 't': out += ' '; break;
            case 'u': {
                if (i + 4 < j.size()) {
                    int cp = (int)strtol(j.substr(i + 1, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 128) out += (char)cp;
                    else out += '?';   // 非 ASCII（中文）走下面的原始 UTF-8 分支不会出现
                }
                break;
            }
            default: out += n; break;
            }
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// 取最新 release：tag / 组合包直链 / 说明
inline Latest FetchLatest(const std::string& proxyUtf8, std::size_t notesMaxChars = 4000) {
    Latest r;
    const std::wstring host = L"api.github.com";
    const std::wstring path = L"/repos/2749478981/WeaponSoundEnhance/releases/latest";
    std::string body, err;
    if (!HttpGetString(host, path, proxyUtf8, true, body, err)) {
        r.err = err;
        return r;
    }
    r.tag = JsonStr(body, "tag_name");
    if (r.tag.empty()) { r.err = "仓库返回内容里没有 tag_name"; return r; }

    // 组合包直链：找含 .zip 的 browser_download_url
    size_t pos = 0;
    while (pos != std::string::npos) {
        const std::string u = JsonStr(body, "browser_download_url", pos);
        if (u.empty()) break;
        if (u.size() > 4 && u.compare(u.size() - 4, 4, ".zip") == 0) { r.zipUrl = u; break; }
        pos = body.find("browser_download_url", pos);
        if (pos == std::string::npos) break;
        pos += 1;
    }
    std::string notes = JsonStr(body, "body");
    if (notes.size() > notesMaxChars) notes.resize(notesMaxChars);
    r.notes = notes;
    r.ok = true;
    return r;
}

} // namespace wseupd
