#include "user_ledger.h"
#include "utils.h"

#include <windows.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;

void UserLedger::setDataDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dataDir_ == dir) {
        return;
    }

    dataDir_ = dir;
    loadFromDisk();
}

// Crypto Helpers (Windows BCrypt)

std::string UserLedger::generateSalt() {
    UCHAR saltBytes[16];
    NTSTATUS status = BCryptGenRandom(NULL, saltBytes, sizeof(saltBytes),
                                       BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        return utils::generateSessionId().substr(0, 32);
    }

    std::ostringstream oss;
    for (int i = 0; i < 16; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)saltBytes[i];
    }
    return oss.str();
}

std::string UserLedger::hashPassword(const std::string& passphrase, const std::string& salt) {
    std::string input = salt + ":" + passphrase;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    UCHAR hashResult[32];
    DWORD hashObjSize = 0, dataSize = 0;

    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!hAlg) return "";

    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(DWORD), &dataSize, 0);
    std::vector<UCHAR> hashObj(hashObjSize);

    BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, NULL, 0, 0);
    if (!hHash) { BCryptCloseAlgorithmProvider(hAlg, 0); return ""; }

    BCryptHashData(hHash, (PUCHAR)input.c_str(), (ULONG)input.size(), 0);
    BCryptFinishHash(hHash, hashResult, sizeof(hashResult), 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream oss;
    for (int i = 0; i < 32; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
    }
    return oss.str();
}

bool UserLedger::verifyPassword(const std::string& passphrase,
                                 const std::string& salt,
                                 const std::string& expectedHash) {
    return hashPassword(passphrase, salt) == expectedHash;
}

// Persistence

void UserLedger::saveToDisk() {
    if (dataDir_.empty()) return;

    json j = json::object();
    for (auto& [id, rec] : users_) {
        if (rec.passwordHash.empty()) continue;  // don't save unclaimed
        j[id] = {
            {"name", rec.name},
            {"hash", rec.passwordHash},
            {"salt", rec.salt},
            {"api_key", rec.savedApiKey},
            {"provider", rec.savedProvider}
        };
    }

    std::string path = dataDir_ + "/ledger.json";
    std::ofstream f(path, std::ios::trunc);
    if (f.is_open()) {
        f << j.dump(2);
        std::cout << "[Elpis] Ledger saved to disk" << std::endl;
    }
}

void UserLedger::loadFromDisk() {
    if (dataDir_.empty()) return;

    std::string path = dataDir_ + "/ledger.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;
        int loaded = 0;
        for (auto& [id, data] : j.items()) {
            auto it = users_.find(id);
            if (it != users_.end()) {
                it->second.passwordHash  = data.value("hash", "");
                it->second.salt          = data.value("salt", "");
                it->second.savedApiKey   = data.value("api_key", "");
                it->second.savedProvider = data.value("provider", "");
                if (!it->second.passwordHash.empty()) loaded++;
            }
        }
        std::cout << "[Elpis] Loaded " << loaded << " claimed accounts from disk" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Elpis] Failed to load ledger: " << e.what() << std::endl;
    }
}

std::string UserLedger::resolveTemplatePath(const std::string& studentId) {
    if (dataDir_.empty()) return "./templates/profiles/default_academic.html";

    // Path convention: templates/profiles/<ID>.html
    std::string customPath = dataDir_ + "/templates/profiles/" + studentId + ".html";
    if (std::filesystem::exists(customPath)) {
        return customPath;
    }

    // Fallback to default academic (stable baseline), otherwise allow modern lab.
    std::string academic = dataDir_ + "/templates/profiles/default_academic.html";
    if (std::filesystem::exists(academic)) {
        return academic;
    }
    std::string modern = dataDir_ + "/templates/profiles/default_modern_lab.html";
    if (std::filesystem::exists(modern)) {
        return modern;
    }
    return academic;
}

// Ledger Initialization

UserLedger::UserLedger() {
    struct Entry { const char* id; const char* name; };
    static const Entry entries[] = {
        { "202453460009", "Wassi Muhammad" },
        { "202453460034", "Waqas Muhammad" },
        { "202453460047", "EL BARNOUSSI FATIMA EZZAHRA" },
        { "202453460052", "Tumpa Jannatul Fardous" },
        { "202453460073", "Nadeem Ahsen" },
    };

    for (auto& e : entries) {
        UserRecord rec;
        rec.studentId    = e.id;
        rec.name         = e.name;
        rec.passwordHash = "";
        rec.salt         = "";
        rec.savedApiKey  = "";
        rec.savedProvider = "";
        users_[rec.studentId] = rec;
    }

    std::cout << "[Elpis] Sovereign Ledger initialized with "
              << users_.size() << " students" << std::endl;
}

// Account Claiming

std::string UserLedger::claim(const std::string& studentId, const std::string& passphrase) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(studentId);
    if (it == users_.end()) {
        return "Student ID not found in the ledger.";
    }

    if (!it->second.passwordHash.empty()) {
        return "This account has already been claimed.";
    }

    if (passphrase.size() < 4) {
        return "Passphrase must be at least 4 characters.";
    }

    std::string salt = generateSalt();
    std::string hash = hashPassword(passphrase, salt);

    it->second.salt = salt;
    it->second.passwordHash = hash;

    std::cout << "[Elpis] Account claimed: " << it->second.name
              << " (" << studentId << ")" << std::endl;

    saveToDisk();
    return "";
}

// Login

std::string UserLedger::login(const std::string& studentId,
                               const std::string& passphrase,
                               const std::string& apiKey,
                               std::string& outError) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(studentId);
    if (it == users_.end()) {
        outError = "Student ID not found.";
        return "";
    }

    if (it->second.passwordHash.empty()) {
        outError = "Account not yet claimed. Please claim your account first.";
        return "";
    }

    if (!verifyPassword(passphrase, it->second.salt, it->second.passwordHash)) {
        outError = "Invalid passphrase.";
        return "";
    }

    // Use provided key, or fall back to saved key
    std::string effectiveKey = apiKey.empty() ? it->second.savedApiKey : apiKey;

    if (effectiveKey.empty()) {
        outError = "An API key is required. Get a free key from Groq or Google Gemini.";
        return "";
    }

    // Save the key for future logins
    if (!apiKey.empty()) {
        it->second.savedApiKey = apiKey;
        // Auto-detect provider
        if (apiKey.substr(0, 4) == "gsk_") {
            it->second.savedProvider = "groq";
        } else if (apiKey.substr(0, 4) == "AIza") {
            it->second.savedProvider = "gemini";
        } else if (apiKey.substr(0, 3) == "sk-") {
            it->second.savedProvider = "openai";
        }
        saveToDisk();
    }

    // Invalidate any existing session for this student
    for (auto sit = sessions_.begin(); sit != sessions_.end(); ) {
        if (sit->second.studentId == studentId) {
            sit = sessions_.erase(sit);
        } else {
            ++sit;
        }
    }

    // Create new session
    AuthSession session;
    session.token     = utils::generateSessionId() + utils::generateSessionId();
    session.studentId = studentId;
    session.name      = it->second.name;
    session.apiKey    = effectiveKey;
    session.provider  = it->second.savedProvider;
    session.createdAt = std::chrono::steady_clock::now();

    sessions_[session.token] = session;

    std::cout << "[Elpis] Login: " << session.name
              << " | Provider: " << session.provider
              << " | Session: " << session.token.substr(0, 8) << "..." << std::endl;

    return session.token;
}

// Session Validation

std::optional<AuthSession> UserLedger::validateSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(token);
    if (it == sessions_.end()) return std::nullopt;

    // Auto-expire sessions after 24 hours (easier for users)
    auto elapsed = std::chrono::steady_clock::now() - it->second.createdAt;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(elapsed).count();
    if (hours >= 24) {
        sessions_.erase(it);
        return std::nullopt;
    }

    return it->second;
}

// Logout

void UserLedger::logout(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it != sessions_.end()) {
        std::cout << "[Elpis] Logout: " << it->second.name << std::endl;
        sessions_.erase(it);
    }
}

// Accessors

std::optional<UserRecord> UserLedger::getUser(const std::string& studentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(studentId);
    if (it == users_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<std::string, std::string>> UserLedger::getStudentList() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> list;
    for (auto& [id, rec] : users_) {
        list.push_back({id, rec.name});
    }
    return list;
}

bool UserLedger::hasSavedKey(const std::string& studentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(studentId);
    return (it != users_.end() && !it->second.savedApiKey.empty());
}
