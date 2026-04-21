#pragma once
#include <string>
#include <map>
#include <mutex>
#include "user_ledger.h"

// ── Session State Struct ───────────────────────────────────────────────────
struct SessionState {
    std::string status = "Initializing...";
    bool isComplete = false;
    bool isError = false;
    std::string pdfUrl = "";
    std::string reportTitle = "";
};

// ── HTTP Server ────────────────────────────────────────────────────────────
// Sets up routes and starts the cpp-httplib server.

class Server {
public:
    Server();

    void setPort(int port) { port_ = port; }
    void setApiKey(const std::string& key) { apiKey_ = key; }

    // Blocking: starts the server and listens.
    void start();

private:
    int port_ = 8000;
    std::string apiKey_;
    std::string baseDir_;   // executable directory

    // Thread-safe session tracking
    std::map<std::string, SessionState> sessions_;
    std::mutex sessionsMutex_;

    // Sovereign Ledger — multi-user auth & BYOK
    UserLedger ledger_;
};
