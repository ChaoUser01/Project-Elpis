#pragma once
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <vector>

// ── User record in the Sovereign Ledger ────────────────────────────────────

struct UserRecord {
    std::string studentId;
    std::string name;
    std::string passwordHash;   // SHA-256 hex digest (empty = unclaimed)
    std::string salt;           // Random salt hex (empty = unclaimed)
    std::string savedApiKey;    // Persisted API key (saved on login)
    std::string savedProvider;  // "groq", "gemini", "openai"
};

// ── Authenticated session ──────────────────────────────────────────────────

struct AuthSession {
    std::string token;
    std::string studentId;
    std::string name;
    std::string apiKey;         // BYOK — active session key
    std::string provider;       // "groq", "gemini", "openai"
    std::chrono::steady_clock::time_point createdAt;
};

// ── The Sovereign Ledger ───────────────────────────────────────────────────
// Thread-safe user authentication and session management.
// Persists password hashes and API keys to a JSON file on disk.

class UserLedger {
public:
    UserLedger();

    // Set the data directory for persistence
    void setDataDir(const std::string& dir) { dataDir_ = dir; }

    // Phase 1: Account Claiming
    std::string claim(const std::string& studentId, const std::string& passphrase);

    // Phase 1: Login with BYOK (apiKey can be empty to reuse saved key)
    std::string login(const std::string& studentId,
                      const std::string& passphrase,
                      const std::string& apiKey,
                      std::string& outError);

    // Session validation
    const AuthSession* validateSession(const std::string& token);

    // Logout
    void logout(const std::string& token);

    // Get user record by student ID
    const UserRecord* getUser(const std::string& studentId);

    // Get all student IDs
    std::vector<std::pair<std::string, std::string>> getStudentList();

    // Check if user has a saved API key
    bool hasSavedKey(const std::string& studentId);

private:
    std::map<std::string, UserRecord> users_;
    std::map<std::string, AuthSession> sessions_;
    std::mutex mutex_;
    std::string dataDir_;

    // Crypto helpers
    std::string generateSalt();
    std::string hashPassword(const std::string& passphrase, const std::string& salt);
    bool verifyPassword(const std::string& passphrase,
                        const std::string& salt,
                        const std::string& expectedHash);

    // Persistence
    void saveToDisk();
    void loadFromDisk();
};
