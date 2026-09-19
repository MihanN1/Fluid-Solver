#include "UpdateCheck.hpp"

#include <thread>

// The window is released from the same repository as the solver, in the same
// set of archives, so it asks the same place. Spelled out here rather than
// pulled from the solver's Version.hpp, which this build does not include.
#ifndef CFD_RELEASES_URL
#define CFD_RELEASES_URL \
    "https://github.com/MihanN1/Fluid-Solver/releases"
#endif
#ifndef CFD_RELEASES_API
#define CFD_RELEASES_API \
    "https://api.github.com/repos/MihanN1/Fluid-Solver/releases"
#endif

#include <array>
#include <cctype>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#endif

namespace update {
namespace {

// ---------------------------------------------------------------- version ---

std::vector<long> numbers(const std::string& tag) {
    std::vector<long> parts;
    size_t i = 0;
    // A leading "v" is how git tags are spelled here; the version itself is
    // not.
    if (i < tag.size() && (tag[i] == 'v' || tag[i] == 'V'))
        ++i;
    long current = 0;
    bool inNumber = false;
    for (; i < tag.size(); ++i) {
        const char c = tag[i];
        if (c >= '0' && c <= '9') {
            // Overflow would need a 19-digit component; clamp rather than wrap.
            if (current < 100000000L)
                current = current * 10 + (c - '0');
            inNumber = true;
        } else if (c == '.') {
            parts.push_back(inNumber ? current : 0);
            current = 0;
            inNumber = false;
        } else {
            // "0.3-beta", "0.3rc1": the numbers before the suffix are the
            // ordering, and the suffix is left to the tag itself.
            break;
        }
    }
    if (inNumber)
        parts.push_back(current);
    return parts;
}

// ------------------------------------------------------------------ fetch ---

const char* kUserAgent = "Fluid Solver UI/" CFD_MASK_UI_VERSION;

#if defined(_WIN32)

std::wstring widen(const std::string& text) {
    if (text.empty())
        return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

bool httpGet(const std::string& url, int timeoutSeconds, std::string& body,
             std::string& error) {
    const std::wstring wideUrl = widen(url);

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        error = "malformed URL";
        return false;
    }

    const DWORD ms = static_cast<DWORD>(timeoutSeconds) * 1000;
    HINTERNET session = WinHttpOpen(widen(kUserAgent).c_str(),
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHttpOpen failed";
        return false;
    }
    WinHttpSetTimeouts(session, static_cast<int>(ms), static_cast<int>(ms),
                       static_cast<int>(ms), static_cast<int>(ms));

    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection) {
        HINTERNET request = WinHttpOpenRequest(
            connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
        if (request) {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0;
                DWORD size = sizeof(status);
                WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_STATUS_CODE |
                                        WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                    &size, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    std::array<char, 8192> buffer{};
                    DWORD read = 0;
                    // The releases list is a few tens of kilobytes; a cap keeps
                    // a redirected-to-something-else response from filling
                    // memory.
                    while (body.size() < 1u << 20 &&
                           WinHttpReadData(request, buffer.data(),
                                           static_cast<DWORD>(buffer.size()),
                                           &read) &&
                           read > 0) {
                        body.append(buffer.data(), read);
                    }
                    ok = true;
                } else {
                    error = "HTTP " + std::to_string(status);
                }
            } else {
                error = "the request did not complete";
            }
            WinHttpCloseHandle(request);
        } else {
            error = "WinHttpOpenRequest failed";
        }
        WinHttpCloseHandle(connection);
    } else {
        error = "could not reach " + url;
    }
    WinHttpCloseHandle(session);
    return ok;
}

#else

// No HTTPS client is part of the C++ library and none of the three platforms
// ship one that can be linked into a static binary, so the machine's own
// downloader is used. curl is on every macOS and nearly every Linux; wget
// covers the rest. Neither being there is not an error - it just means no
// check.
bool runCapture(const std::string& command, std::string& body) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return false;
    std::array<char, 4096> buffer{};
    size_t read = 0;
    while (body.size() < 1u << 20 &&
           (read = fread(buffer.data(), 1, buffer.size(), pipe)) > 0) {
        body.append(buffer.data(), read);
    }
    const int status = pclose(pipe);
    return status == 0 && !body.empty();
}

bool haveCommand(const char* name) {
    const std::string probe =
        std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

bool httpGet(const std::string& url, int timeoutSeconds, std::string& body,
             std::string& error) {
    const std::string timeout = std::to_string(timeoutSeconds);
    if (haveCommand("curl")) {
        const std::string command =
            "curl -fsSL --max-time " + timeout + " -A '" + kUserAgent + "' '" +
            url + "' 2>/dev/null";
        if (runCapture(command, body))
            return true;
        body.clear();
    }
    if (haveCommand("wget")) {
        const std::string command =
            "wget -q -O - --timeout=" + timeout + " --tries=1 -U '" +
            kUserAgent + "' '" + url + "' 2>/dev/null";
        if (runCapture(command, body))
            return true;
        body.clear();
    }
    error = "no curl or wget on this machine, or the request failed";
    return false;
}

#endif

// ------------------------------------------------------------------ parse ---

// Every "tag_name": "..." in the response, without pulling in a JSON library
// for six characters of structure. Draft releases carry a tag too, so
// "draft": true entries are dropped by looking at the object they sit in.
std::vector<std::string> tagsIn(const std::string& json) {
    std::vector<std::string> tags;
    const std::string key = "\"tag_name\"";
    size_t at = 0;
    while ((at = json.find(key, at)) != std::string::npos) {
        size_t i = json.find(':', at + key.size());
        if (i == std::string::npos)
            break;
        ++i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])))
            ++i;
        if (i >= json.size() || json[i] != '"') {
            at += key.size();
            continue;
        }
        const size_t start = ++i;
        while (i < json.size() && json[i] != '"')
            ++i;
        std::string tag = json.substr(start, i - start);

        // The "draft" flag belongs to the same object, and GitHub puts it
        // after the tag. Looking only as far as the next tag keeps this from
        // reading the following release's flag.
        const size_t next = json.find(key, i);
        const size_t limit = (next == std::string::npos) ? json.size() : next;
        const size_t draft = json.find("\"draft\"", i);
        bool isDraft = false;
        if (draft != std::string::npos && draft < limit)
            isDraft = json.compare(json.find_first_not_of(": ", draft + 7), 4,
                                   "true") == 0;
        if (!isDraft && !tag.empty())
            tags.push_back(std::move(tag));
        at = i;
    }
    return tags;
}

std::string stripV(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        return tag.substr(1);
    return tag;
}

bool envOff(const char* name) {
    const char* value = std::getenv(name);
    return value && *value && !(value[0] == '0' && value[1] == '\0');
}

}   // namespace

int compareVersions(const std::string& a, const std::string& b) {
    const std::vector<long> left = numbers(a);
    const std::vector<long> right = numbers(b);
    const size_t count = (left.size() > right.size()) ? left.size() : right.size();
    for (size_t i = 0; i < count; ++i) {
        const long l = (i < left.size()) ? left[i] : 0;
        const long r = (i < right.size()) ? right[i] : 0;
        if (l < r) return -1;
        if (l > r) return 1;
    }
    return 0;
}

Result check(int timeoutSeconds) {
    Result result;
    result.url = CFD_RELEASES_URL;

    std::string body;
    if (!httpGet(CFD_RELEASES_API "?per_page=30", timeoutSeconds, body,
                 result.error))
        return result;

    result.checked = true;

    std::string best;
    for (const std::string& tag : tagsIn(body)) {
        const std::string version = stripV(tag);
        if (best.empty() || compareVersions(version, best) > 0)
            best = version;
    }
    if (best.empty()) {
        result.error = "the response held no releases";
        result.checked = false;
        return result;
    }

    result.latest = best;
    result.newer = compareVersions(best, CFD_MASK_UI_VERSION) > 0;
    if (result.newer)
        result.url = std::string(CFD_RELEASES_URL) + "/tag/v" + best;
    return result;
}

bool openUrl(const std::string& url) {
#if defined(_WIN32)
    const std::wstring wide = widen(url);
    const HINSTANCE code =
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(code) > 32;
#elif defined(__APPLE__)
    const std::string command = "open '" + url + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#else
    const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#endif
}

// Off the window's thread, always. The check is one HTTPS request with a short
// timeout, but a machine with no route out can still sit on a DNS lookup for
// seconds, and the window has to keep drawing through it. Detached, because
// there is nothing to wait for: if the answer arrives after the window closes,
// nobody is listening and that is fine.
void checkInBackground(std::function<void(Result)> done) {
    if (envOff("FLUID_SOLVER_NO_UPDATE_CHECK")) {
        return;
    }
    std::thread([done = std::move(done)]() {
        Result result = check();
        if (done) {
            done(std::move(result));
        }
    }).detach();
}


}   // namespace update
