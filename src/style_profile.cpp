#include "style_profile.h"
#include "utils.h"

StyleProfile generateStyleProfile(const std::string& sessionId) {
    uint32_t h = utils::hashString(sessionId);
    StyleProfile s;

    // Font families
    struct FontSet { const char* body; const char* bold; const char* italic; const char* heading; };
    static const FontSet fonts[] = {
        { "Times-Roman",  "Times-Bold",     "Times-Italic",        "Helvetica-Bold" },
        { "Times-Roman",  "Times-Bold",     "Times-Italic",        "Times-Bold"     },
        { "Helvetica",    "Helvetica-Bold", "Helvetica-Oblique",   "Helvetica-Bold" },
        { "Times-Roman",  "Times-Bold",     "Times-Italic",        "Courier-Bold"   },
        { "Helvetica",    "Helvetica-Bold", "Helvetica-Oblique",   "Times-Bold"     },
    };
    int fi = h % 5;
    s.bodyFont      = fonts[fi].body;
    s.bodyFontBold  = fonts[fi].bold;
    s.bodyFontItalic= fonts[fi].italic;
    s.headingFont   = fonts[fi].heading;
    s.codeFont      = "Courier";
    s.codeFontBold  = "Courier-Bold";

    // Accent colours
    struct Colour { float r, g, b; };
    static const Colour accents[] = {
        { 0.10f, 0.15f, 0.40f },   // navy
        { 0.50f, 0.10f, 0.15f },   // burgundy
        { 0.12f, 0.33f, 0.18f },   // forest green
        { 0.25f, 0.25f, 0.28f },   // charcoal
        { 0.30f, 0.35f, 0.45f },   // slate blue
        { 0.40f, 0.22f, 0.10f },   // sepia
        { 0.20f, 0.30f, 0.35f },   // teal grey
        { 0.35f, 0.15f, 0.35f },   // plum
    };
    int ci = (h >> 4) % 8;
    s.accentR = accents[ci].r;
    s.accentG = accents[ci].g;
    s.accentB = accents[ci].b;

    // Heading style
    s.headingStyle = static_cast<StyleProfile::HeadingStyle>((h >> 8) % 4);

    // Border style
    s.borderStyle = static_cast<StyleProfile::BorderStyle>((h >> 10) % 4);
    static const float borderWidths[] = { 0.5f, 0.75f, 1.0f, 1.5f };
    s.borderWidth = borderWidths[(h >> 12) % 4];

    // Drop cap
    s.useDropCap = ((h >> 14) % 3) == 0;  // ~33% chance

    // Font sizes
    static const float titleSizes[]   = { 22.0f, 24.0f, 20.0f, 26.0f };
    static const float headingSizes[] = { 14.0f, 15.0f, 13.0f, 16.0f };
    static const float bodySizes[]    = { 11.0f, 11.5f, 12.0f, 10.5f };
    static const float codeSizes[]    = { 9.0f,  9.5f,  10.0f, 8.5f  };

    int si = (h >> 16) % 4;
    s.titleFontSize   = titleSizes[si];
    s.headingFontSize = headingSizes[si];
    s.bodyFontSize    = bodySizes[si];
    s.codeFontSize    = codeSizes[si];

    // Page numbering
    s.pageNumStyle = static_cast<StyleProfile::PageNumStyle>((h >> 18) % 3);

    if (sessionId == "202453460047") {
        s.bodyFont       = "Times-Roman";
        s.bodyFontBold   = "Times-Bold";
        s.bodyFontItalic = "Times-Italic";
        s.headingFont    = "Helvetica-Bold";
        s.accentR = 0.08f;
        s.accentG = 0.08f;
        s.accentB = 0.08f;
        s.headingStyle = StyleProfile::HeadingStyle::Rule;
        s.borderStyle = StyleProfile::BorderStyle::Solid;
        s.borderWidth = 0.75f;
        s.useDropCap = false;
        s.titleFontSize = 24.0f;
        s.headingFontSize = 14.0f;
        s.bodyFontSize = 11.0f;
        s.codeFontSize = 9.0f;
        s.pageNumStyle = StyleProfile::PageNumStyle::BottomCenter;
    }

    return s;
}
