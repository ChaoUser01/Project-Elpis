#include "http_client.h"

#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Helpers

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

struct UrlParts {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 443;
    std::wstring path;
};

static UrlParts parseUrl(const std::string& url) {
    UrlParts p;
    std::wstring wurl = toWide(url);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);

    wchar_t hostBuf[256] = {}, pathBuf[2048] = {};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;

    WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc);

    p.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    p.host = hostBuf;
    p.port = uc.nPort;
    p.path = pathBuf;
    return p;
}

// Implementation

HttpClient::Response HttpClient::post(const std::string& url,
                                       const std::string& body,
                                       const std::map<std::string, std::string>& headers) {
    return request("POST", url, body, headers);
}

HttpClient::Response HttpClient::get(const std::string& url,
                                      const std::map<std::string, std::string>& headers) {
    return request("GET", url, "", headers);
}

HttpClient::Response HttpClient::request(const std::string& method,
                                          const std::string& url,
                                          const std::string& body,
                                          const std::map<std::string, std::string>& headers) {
    Response resp;
    auto parts = parseUrl(url);

    HINTERNET hSession = WinHttpOpen(L"Elpis/2.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0);
    if (!hSession) { resp.body = "WinHttpOpen failed"; return resp; }

    // Set timeouts: 30 second resolve, 30s connect, 60s send, 120s receive
    WinHttpSetTimeouts(hSession, 30000, 30000, 60000, 120000);

    HINTERNET hConnect = WinHttpConnect(hSession, parts.host.c_str(), parts.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpConnect failed";
        return resp;
    }

    std::wstring wmethod = toWide(method);
    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(),
                                             parts.path.c_str(),
                                             nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpOpenRequest failed";
        return resp;
    }

    // Add headers
    for (auto& [key, val] : headers) {
        std::wstring hdr = toWide(key + ": " + val);
        WinHttpAddRequestHeaders(hRequest, hdr.c_str(), (DWORD)-1L,
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send request
    BOOL result = WinHttpSendRequest(hRequest,
                                      WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                   : (LPVOID)body.c_str(),
                                      (DWORD)body.size(),
                                      (DWORD)body.size(),
                                      0);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpSendRequest failed (error " + std::to_string(GetLastError()) + ")";
        return resp;
    }

    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpReceiveResponse failed";
        return resp;
    }

    // Read status code
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<int>(statusCode);

    // Read body
    std::ostringstream oss;
    DWORD bytesAvailable = 0;
    do {
        bytesAvailable = 0;
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable == 0) break;

        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead);
        oss.write(buf.data(), bytesRead);
    } while (bytesAvailable > 0);

    resp.body = oss.str();

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}
