#pragma once
#include <string>

class HtmlConverter {
public:
    // Converts an HTML file to a PDF file using MS Edge headless mode.
    // Returns true on success.
    static bool convertToPdf(const std::string& htmlPath, const std::string& pdfPath);

private:
    static std::string getEdgePath();
};
