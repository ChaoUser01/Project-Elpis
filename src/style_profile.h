#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Academic style profile

struct StyleProfile {
    std::string bodyFont;       // e.g. "Times-Roman"
    std::string bodyFontBold;   // e.g. "Times-Bold"
    std::string bodyFontItalic; // e.g. "Times-Italic"
    std::string headingFont;    // e.g. "Helvetica-Bold"
    std::string codeFont;       // always "Courier"
    std::string codeFontBold;   // always "Courier-Bold"

    float accentR, accentG, accentB;  // Accent colour (0-1 range)

    enum class HeadingStyle { Underline, SmallCaps, Rule, BoxedRule };
    HeadingStyle headingStyle;

    enum class BorderStyle { Solid, Double, Dashed, None };
    BorderStyle borderStyle;
    float borderWidth;  // pt

    bool  useDropCap;
    float titleFontSize;    // pt
    float headingFontSize;  // pt
    float bodyFontSize;     // pt
    float codeFontSize;     // pt

    // Page numbering style
    enum class PageNumStyle { BottomCenter, BottomRight, TopRight };
    PageNumStyle pageNumStyle;
};

// Generate a deterministic style from a session ID (same ID → same style).
StyleProfile generateStyleProfile(const std::string& sessionId);
