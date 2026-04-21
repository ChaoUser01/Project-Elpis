#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ── OCR Engine ─────────────────────────────────────────────────────────────
// Attempts to extract text from images via:
//  1. Tesseract C API (dynamically loaded — no build-time dependency)
//  2. Fallback: returns descriptive error

class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();

    bool isAvailable() const { return available_; }

    // Extract text from image bytes (JPEG or PNG).
    // Returns extracted text, or an error message prefixed with "[OCR Error]".
    std::string extractText(const std::vector<uint8_t>& imageBytes);

private:
    bool available_ = false;

    // Dynamic Tesseract loading (Windows)
    void* hModule_ = nullptr;
    void* api_     = nullptr;
};
