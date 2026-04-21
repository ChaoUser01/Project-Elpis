#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace base64 {

std::string encode(const std::vector<uint8_t>& data);
std::string encode(const std::string& data);
std::vector<uint8_t> decode(const std::string& encoded);

} // namespace base64
