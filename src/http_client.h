#pragma once
#include <string>
#include <map>

// WinHTTP-based HTTPS client

class HttpClient {
public:
    struct Response {
        int status = 0;
        std::string body;
        bool ok() const { return status >= 200 && status < 300; }
    };

    HttpClient() = default;

    Response post(const std::string& url,
                  const std::string& body,
                  const std::map<std::string, std::string>& headers = {});

    Response get(const std::string& url,
                 const std::map<std::string, std::string>& headers = {});

private:
    Response request(const std::string& method,
                     const std::string& url,
                     const std::string& body,
                     const std::map<std::string, std::string>& headers);
};
