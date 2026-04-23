#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace utils {

//String utilities
std::string trim(const std::string& s);
std::string toLower(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string replace(const std::string& s, const std::string& from, const std::string& to);
std::string join(const std::vector<std::string>& parts, const std::string& delim);
std::string extractJson(const std::string& input);
std::string escapeHtml(const std::string& input);

//File I/O
std::string readFile(const std::string& path);
std::vector<uint8_t> readBinaryFile(const std::string& path);
void writeFile(const std::string& path, const std::string& content);
void writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data);
bool fileExists(const std::string& path);

//Directory
void ensureDir(const std::string& path);
std::string getExecutableDir();

//ID generation
std::string generateSessionId();
std::string generateFilename(const std::string& prefix, const std::string& ext);

//Hash
uint32_t hashString(const std::string& s);

//PDF helpers
std::string escPdf(const std::string& s);

}
