#include "base64.h"

namespace base64 {

static const char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(TABLE[(n >> 18) & 0x3F]);
        out.push_back(TABLE[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < data.size()) ? TABLE[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < data.size()) ? TABLE[n & 0x3F] : '=');
    }
    return out;
}

std::string encode(const std::string& data) {
    return encode(std::vector<uint8_t>(data.begin(), data.end()));
}

std::vector<uint8_t> decode(const std::string& encoded) {
    static int inv[256] = {0};
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) inv[i] = -1;
        for (int i = 0; i < 64; i++) inv[static_cast<unsigned char>(TABLE[i])] = i;
        inv[static_cast<unsigned char>('=')] = 0;
        init = true;
    }

    std::vector<uint8_t> out;
    out.reserve((encoded.size() / 4) * 3);

    for (size_t i = 0; i + 3 < encoded.size(); i += 4) {
        uint32_t n = (inv[static_cast<unsigned char>(encoded[i])] << 18) |
                     (inv[static_cast<unsigned char>(encoded[i+1])] << 12) |
                     (inv[static_cast<unsigned char>(encoded[i+2])] << 6) |
                      inv[static_cast<unsigned char>(encoded[i+3])];
        out.push_back((n >> 16) & 0xFF);
        if (encoded[i+2] != '=') out.push_back((n >> 8) & 0xFF);
        if (encoded[i+3] != '=') out.push_back(n & 0xFF);
    }
    return out;
}

} // namespace base64
