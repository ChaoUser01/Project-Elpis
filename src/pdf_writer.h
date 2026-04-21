#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <sstream>

// Custom PDF 1.4 Generator
// Generates compliant PDF documents using only the 14 standard PDF fonts.

class PdfWriter {
public:
    // Page dimensions in mm. A4 = 210 x 297.
    PdfWriter(float pageW_mm = 210.0f, float pageH_mm = 297.0f);

    // Page management
    void newPage();
    void setMargins(float top_mm, float right_mm, float bottom_mm, float left_mm);

    // Text commands
    // Standard PDF font names: Times-Roman, Times-Bold, Times-Italic,
    // Times-BoldItalic, Helvetica, Helvetica-Bold, Helvetica-Oblique,
    // Helvetica-BoldOblique, Courier, Courier-Bold, Courier-Oblique,
    // Courier-BoldOblique, Symbol, ZapfDingbats
    void setFont(const std::string& name, float sizePt);
    void setColor(float r, float g, float b);
    void setTextColor(float r, float g, float b);

    // Draw text at absolute position (in mm from top-left).
    // Returns the width of the drawn text in mm.
    float drawText(float x_mm, float y_mm, const std::string& text);

    // Draw word-wrapped text within a column. Returns the Y position after
    // the last line (in mm from top). Automatically creates new pages.
    float drawWrappedText(float x_mm, float y_mm, float maxWidth_mm,
                          const std::string& text, float lineSpacing = 1.4f);

    // Drawing primitives
    void drawLine(float x1_mm, float y1_mm, float x2_mm, float y2_mm,
                  float width_pt = 0.5f);
    void drawRect(float x_mm, float y_mm, float w_mm, float h_mm,
                  bool fill = false, float lineWidth_pt = 0.5f);

    // Image handling
    // Embed a JPEG image. Data must be raw JPEG bytes.
    void embedJpeg(float x_mm, float y_mm, float w_mm, float h_mm,
                   const std::vector<uint8_t>& jpegData,
                   int imgWidth, int imgHeight);

    // Layout accessors
    float pageWidth()  const { return pageW_; }
    float pageHeight() const { return pageH_; }
    float marginTop()    const { return mTop_; }
    float marginRight()  const { return mRight_; }
    float marginBottom() const { return mBottom_; }
    float marginLeft()   const { return mLeft_; }
    float contentWidth() const { return pageW_ - mLeft_ - mRight_; }
    float contentTop()   const { return mTop_; }
    float contentBottom() const { return pageH_ - mBottom_; }
    int   pageCount() const { return static_cast<int>(pages_.size()); }
    float currentFontSize() const { return fontSize_; }
    std::string currentFontName() const { return fontName_; }

    // Text measurement
    float stringWidth(const std::string& text) const;
    float stringWidth(const std::string& text, const std::string& font, float size) const;
    float lineHeight(float spacing = 1.4f) const;

    // File output
    void save(const std::string& filename);
    std::vector<uint8_t> toBytes();

private:
    // Internal types
    struct PdfObject {
        int id;
        std::string content;
        size_t byteOffset = 0;
    };

    struct ImageXObject {
        int objectId;
        std::string name;  // e.g., "/Img1"
    };

    struct Page {
        std::ostringstream contentStream;
        std::vector<ImageXObject> images;
    };

    // Coordinate conversion
    float mmToPt(float mm) const { return mm * 2.83465f; }
    // PDF Y-axis is bottom-up; convert from top-down mm to PDF pt
    float yToPdf(float y_mm) const { return mmToPt(pageH_ - y_mm); }

    // Font metrics
    float charWidth(unsigned char c, const std::string& font, float sizePt) const;
    void initFontMetrics();

    // PDF building logic
    int allocObject();
    Page& currentPage();
    std::string buildFontResources(int pageIdx);
    std::string buildPdf();

    int ensureFontObject(const std::string& fontName);

    // Instance state
    float pageW_, pageH_;                          // mm
    float mTop_, mRight_, mBottom_, mLeft_;        // mm

    std::string fontName_  = "Times-Roman";
    float       fontSize_  = 12.0f;
    float       colorR_ = 0, colorG_ = 0, colorB_ = 0;
    float       textR_  = 0, textG_  = 0, textB_  = 0;

    std::vector<Page> pages_;
    int nextObjId_ = 1;

    // Font name → PDF object ID
    std::map<std::string, int> fontObjects_;
    // Ordered list of font names for resource dictionary
    std::vector<std::string> fontOrder_;

    // Character width tables (in 1/1000 of a unit, standard AFM data)
    // Key: font base name → 256-entry width array
    std::map<std::string, std::vector<int>> fontMetrics_;

    // Image data storage (written during embedJpeg, consumed during buildPdf)
    struct ImageStore { std::vector<uint8_t> data; int width; int height; };
    std::map<int, ImageStore> imageData_;
};
