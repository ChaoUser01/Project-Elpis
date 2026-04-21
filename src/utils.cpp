#include "utils.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <filesystem>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace utils {

//String

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (std::getline(iss, tok, delim)) tokens.push_back(tok);
    return tokens;
}

std::string replace(const std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::string result = s;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.size(), to);
        pos += to.size();
    }
    return result;
}

std::string join(const std::vector<std::string>& parts, const std::string& delim) {
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += delim;
        result += parts[i];
    }
    return result;
}

std::string extractJson(const std::string& input) {
    size_t start = input.find('{');
    size_t end = input.find_last_of('}');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return input.substr(start, end - start + 1);
    }
    return input;
}

// File I/O
std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::in);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    f << content;
}

void writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

bool fileExists(const std::string& path) {
    return fs::exists(path);
}

// Directory utilities
void ensureDir(const std::string& path) {
    fs::create_directories(path);
}

std::string getExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
#else
    return ".";
#endif
}

// Session ID generation
std::string generateSessionId() {
    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    static const char hex[] = "0123456789abcdef";

    std::string id;
    id.reserve(32);
    for (int i = 0; i < 32; i++) {
        id.push_back(hex[rng() % 16]);
    }
    return id;
}

std::string generateFilename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return prefix + "_" + std::to_string(ms) + "." + ext;
}

// Simple FNV-1a hash
uint32_t hashString(const std::string& s) {
    // FNV-1a 32-bit
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        h *= 16777619u;
    }
    return h;
}

// PDF utilities
std::string escPdf(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

} // namespace utils
