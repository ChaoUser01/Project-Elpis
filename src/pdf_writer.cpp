#include "pdf_writer.h"
#include "utils.h"

#include <fstream>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <cstring>

// PdfWriter - Custom PDF 1.4 generator

PdfWriter::PdfWriter(float pageW_mm, float pageH_mm)
    : pageW_(pageW_mm), pageH_(pageH_mm),
      mTop_(25.0f), mRight_(20.0f), mBottom_(25.0f), mLeft_(20.0f)
{
    initFontMetrics();
}

// Font metrics (Adobe AFM data)
// Values are character widths in 1/1000 of a text space unit.
// For brevity we store the most common Latin-1 subset; unmapped chars get
// a default width.

void PdfWriter::initFontMetrics() {
    // Times-Roman: variable-width serif
    // Source: Adobe AFM for Times-Roman (key characters)
    auto& tr = fontMetrics_["Times-Roman"];
    tr.resize(256, 444);  // default width
    // Selected widths from the AFM spec (space through tilde)
    static const int timesR[] = {
     // 32-127 (space .. DEL)
        250, 333, 408, 500, 500, 833, 778, 180, 333, 333,  // sp ! " # $ % & ' ( )
        500, 564, 250, 333, 250, 278, 500, 500, 500, 500,  // * + , - . / 0 1 2 3
        500, 500, 500, 500, 500, 500, 278, 278, 564, 564,  // 4 5 6 7 8 9 : ; < =
        564, 444, 921, 722, 667, 667, 722, 611, 556, 722,  // > ? @ A B C D E F G
        722, 333, 389, 722, 611, 889, 722, 722, 556, 722,  // H I J K L M N O P Q
        667, 556, 611, 722, 722, 944, 722, 722, 611, 333,  // R S T U V W X Y Z [
        278, 333, 469, 500, 333, 444, 500, 444, 500, 444,  // \ ] ^ _ ` a b c d e
        333, 500, 500, 278, 278, 500, 278, 778, 500, 500,  // f g h i j k l m n o
        500, 500, 333, 389, 278, 500, 500, 722, 500, 500,  // p q r s t u v w x y
        444, 480, 200, 480, 541                              // z { | } ~
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(timesR)/sizeof(timesR[0])); i++)
        tr[32 + i] = timesR[i];

    // Times-Bold
    auto& tb = fontMetrics_["Times-Bold"];
    tb.resize(256, 500);
    static const int timesB[] = {
        250, 333, 555, 500, 500, 1000,833, 278, 333, 333,
        500, 570, 250, 333, 250, 278, 500, 500, 500, 500,
        500, 500, 500, 500, 500, 500, 333, 333, 570, 570,
        570, 500, 930, 722, 667, 722, 722, 667, 611, 778,
        778, 389, 500, 778, 667, 944, 722, 778, 611, 778,
        722, 556, 667, 722, 722,1000, 722, 722, 667, 333,
        278, 333, 581, 500, 333, 500, 556, 444, 556, 444,
        333, 500, 556, 278, 333, 556, 278, 833, 556, 500,
        556, 556, 444, 389, 333, 556, 500, 722, 500, 500,
        444, 394, 220, 394, 520
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(timesB)/sizeof(timesB[0])); i++)
        tb[32 + i] = timesB[i];

    // Times-Italic
    auto& ti = fontMetrics_["Times-Italic"];
    ti.resize(256, 444);
    static const int timesI[] = {
        250, 333, 420, 500, 500, 833, 778, 214, 333, 333,
        500, 675, 250, 333, 250, 278, 500, 500, 500, 500,
        500, 500, 500, 500, 500, 500, 333, 333, 675, 675,
        675, 500, 920, 611, 611, 667, 722, 611, 611, 722,
        722, 333, 444, 667, 556, 833, 667, 722, 611, 722,
        611, 500, 556, 722, 611, 833, 611, 556, 556, 389,
        278, 389, 422, 500, 333, 500, 500, 444, 500, 444,
        278, 500, 500, 278, 278, 444, 278, 722, 500, 500,
        500, 500, 389, 389, 278, 500, 444, 667, 444, 444,
        389, 400, 275, 400, 541
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(timesI)/sizeof(timesI[0])); i++)
        ti[32 + i] = timesI[i];

    // Times-BoldItalic
    auto& tbi = fontMetrics_["Times-BoldItalic"];
    tbi.resize(256, 500);
    static const int timesBi[] = {
        250, 389, 555, 500, 500, 833, 778, 278, 333, 333,
        500, 570, 250, 333, 250, 278, 500, 500, 500, 500,
        500, 500, 500, 500, 500, 500, 333, 333, 570, 570,
        570, 500, 832, 667, 667, 667, 722, 667, 667, 722,
        778, 389, 500, 667, 611, 889, 722, 722, 611, 722,
        667, 556, 611, 722, 667, 889, 667, 611, 611, 333,
        278, 333, 570, 500, 333, 500, 500, 444, 500, 444,
        333, 500, 556, 278, 278, 500, 278, 778, 556, 500,
        500, 500, 389, 389, 278, 556, 444, 667, 500, 444,
        389, 348, 220, 348, 570
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(timesBi)/sizeof(timesBi[0])); i++)
        tbi[32 + i] = timesBi[i];

    // Helvetica
    auto& hv = fontMetrics_["Helvetica"];
    hv.resize(256, 556);
    static const int helv[] = {
        278, 278, 355, 556, 556, 889, 667, 191, 333, 333,
        389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
        556, 556, 556, 556, 556, 556, 278, 278, 584, 584,
        584, 556,1015, 667, 667, 722, 722, 667, 611, 778,
        722, 278, 500, 667, 556, 833, 722, 778, 667, 778,
        722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
        278, 278, 469, 556, 333, 556, 556, 500, 556, 556,
        278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
        556, 556, 333, 500, 278, 556, 500, 722, 500, 500,
        500, 334, 260, 334, 584
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(helv)/sizeof(helv[0])); i++)
        hv[32 + i] = helv[i];

    // Helvetica-Bold
    auto& hvb = fontMetrics_["Helvetica-Bold"];
    hvb.resize(256, 611);
    static const int helvB[] = {
        278, 333, 474, 556, 556, 889, 722, 238, 333, 333,
        389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
        556, 556, 556, 556, 556, 556, 333, 333, 584, 584,
        584, 611, 975, 722, 722, 722, 722, 667, 611, 778,
        722, 278, 556, 722, 611, 833, 722, 778, 667, 778,
        722, 667, 611, 722, 667, 944, 667, 667, 611, 333,
        278, 333, 584, 556, 333, 556, 611, 556, 611, 556,
        333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
        611, 611, 389, 556, 333, 611, 556, 778, 556, 556,
        500, 389, 280, 389, 584
    };
    for (int i = 0; i < 95 && i < (int)(sizeof(helvB)/sizeof(helvB[0])); i++)
        hvb[32 + i] = helvB[i];

    // Helvetica-Oblique (same metrics as Helvetica)
    fontMetrics_["Helvetica-Oblique"] = fontMetrics_["Helvetica"];

    // Helvetica-BoldOblique (same metrics as Helvetica-Bold)
    fontMetrics_["Helvetica-BoldOblique"] = fontMetrics_["Helvetica-Bold"];

    // Courier (monospace — all chars 600 units)
    auto& cr = fontMetrics_["Courier"];
    cr.resize(256, 600);

    // Courier-Bold, Courier-Oblique, Courier-BoldOblique — all 600
    fontMetrics_["Courier-Bold"]        = cr;
    fontMetrics_["Courier-Oblique"]     = cr;
    fontMetrics_["Courier-BoldOblique"] = cr;

    // Symbol / ZapfDingbats — default 500
    fontMetrics_["Symbol"].resize(256, 500);
    fontMetrics_["ZapfDingbats"].resize(256, 500);
}

// Measurement logic

float PdfWriter::charWidth(unsigned char c, const std::string& font, float sizePt) const {
    auto it = fontMetrics_.find(font);
    if (it == fontMetrics_.end()) {
        // Fallback: try base name
        if (font.find("Times") != std::string::npos)
            it = fontMetrics_.find("Times-Roman");
        else if (font.find("Helvetica") != std::string::npos)
            it = fontMetrics_.find("Helvetica");
        else if (font.find("Courier") != std::string::npos)
            it = fontMetrics_.find("Courier");
        else
            return sizePt * 0.5f;  // emergency fallback
    }
    if (it == fontMetrics_.end()) return sizePt * 0.5f;
    int w = it->second[c];
    return (w / 1000.0f) * sizePt;
}

float PdfWriter::stringWidth(const std::string& text) const {
    return stringWidth(text, fontName_, fontSize_);
}

float PdfWriter::stringWidth(const std::string& text, const std::string& font, float size) const {
    float w = 0;
    for (unsigned char c : text) {
        w += charWidth(c, font, size);
    }
    // Convert from points to mm
    return w / 2.83465f;
}

float PdfWriter::lineHeight(float spacing) const {
    return (fontSize_ * spacing) / 2.83465f;  // pt → mm
}

// Page handling

void PdfWriter::newPage() {
    pages_.emplace_back();
}

void PdfWriter::setMargins(float top, float right, float bottom, float left) {
    mTop_ = top; mRight_ = right; mBottom_ = bottom; mLeft_ = left;
}

PdfWriter::Page& PdfWriter::currentPage() {
    if (pages_.empty()) newPage();
    return pages_.back();
}

// Text commands

void PdfWriter::setFont(const std::string& name, float sizePt) {
    fontName_ = name;
    fontSize_ = sizePt;
    ensureFontObject(name);
}

void PdfWriter::setColor(float r, float g, float b) {
    colorR_ = r; colorG_ = g; colorB_ = b;
}

void PdfWriter::setTextColor(float r, float g, float b) {
    textR_ = r; textG_ = g; textB_ = b;
}

int PdfWriter::ensureFontObject(const std::string& fontName) {
    auto it = fontObjects_.find(fontName);
    if (it != fontObjects_.end()) return it->second;
    int id = allocObject();
    fontObjects_[fontName] = id;
    fontOrder_.push_back(fontName);
    return id;
}

int PdfWriter::allocObject() {
    return nextObjId_++;
}

float PdfWriter::drawText(float x_mm, float y_mm, const std::string& text) {
    auto& page = currentPage();
    auto& s = page.contentStream;

    // Find font index
    ensureFontObject(fontName_);
    int fi = 0;
    for (int i = 0; i < (int)fontOrder_.size(); i++) {
        if (fontOrder_[i] == fontName_) { fi = i; break; }
    }

    float px = mmToPt(x_mm);
    float py = yToPdf(y_mm);

    s << "BT\n";
    s << std::fixed << std::setprecision(3);
    s << textR_ << " " << textG_ << " " << textB_ << " rg\n";
    s << "/F" << fi << " " << fontSize_ << " Tf\n";
    s << px << " " << py << " Td\n";
    s << "(" << utils::escPdf(text) << ") Tj\n";
    s << "ET\n";

    return stringWidth(text);
}

float PdfWriter::drawWrappedText(float x_mm, float y_mm, float maxWidth_mm,
                                  const std::string& text, float lineSpacing) {
    // Word-wrap
    float lh = lineHeight(lineSpacing);
    float curY = y_mm;

    // Split into words
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (!word.empty()) { words.push_back(word); word.clear(); }
            if (c == '\n') words.push_back("\n");
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty()) words.push_back(word);

    std::string line;
    float lineW = 0;
    float spaceW = stringWidth(" ");
    int linesOnPage = 0;  // for orphan/widow control

    auto flushLine = [&]() {
        if (line.empty()) return;
        // Check if we need a new page
        if (curY + lh > contentBottom()) {
            // Widow control: if we only have 1-2 lines left in a paragraph,
            // carry them to the next page
            newPage();
            curY = mTop_;
            linesOnPage = 0;
        }
        drawText(x_mm, curY, line);
        curY += lh;
        linesOnPage++;
        line.clear();
        lineW = 0;
    };

    for (auto& w : words) {
        if (w == "\n") {
            flushLine();
            continue;
        }
        float ww = stringWidth(w);
        if (lineW > 0 && lineW + spaceW + ww > maxWidth_mm) {
            flushLine();
        }
        if (!line.empty()) {
            line += " ";
            lineW += spaceW;
        }
        line += w;
        lineW += ww;
    }
    flushLine();

    return curY;
}

// Shapes

void PdfWriter::drawLine(float x1, float y1, float x2, float y2, float width) {
    auto& s = currentPage().contentStream;
    s << std::fixed << std::setprecision(3);
    s << colorR_ << " " << colorG_ << " " << colorB_ << " RG\n";
    s << width << " w\n";
    s << mmToPt(x1) << " " << yToPdf(y1) << " m\n";
    s << mmToPt(x2) << " " << yToPdf(y2) << " l\n";
    s << "S\n";
}

void PdfWriter::drawRect(float x, float y, float w, float h, bool fill, float lineWidth) {
    auto& s = currentPage().contentStream;
    s << std::fixed << std::setprecision(3);
    if (fill) {
        s << colorR_ << " " << colorG_ << " " << colorB_ << " rg\n";
    }
    s << colorR_ << " " << colorG_ << " " << colorB_ << " RG\n";
    s << lineWidth << " w\n";
    s << mmToPt(x) << " " << yToPdf(y + h) << " "
      << mmToPt(w) << " " << mmToPt(h) << " re\n";
    s << (fill ? "B" : "S") << "\n";
}

// Images

void PdfWriter::embedJpeg(float x_mm, float y_mm, float w_mm, float h_mm,
                           const std::vector<uint8_t>& jpegData,
                           int imgWidth, int imgHeight) {
    // We'll store the image object info and render the placement command
    int imgObjId = allocObject();

    std::string imgName = "/Img" + std::to_string(imgObjId);
    currentPage().images.push_back({ imgObjId, imgName });

    // Store JPEG data — we'll write the XObject later during PDF assembly
    // For now, tag the image object with its data using a special marker
    // This is handled in buildPdf()

    // Draw the image in the content stream
    auto& s = currentPage().contentStream;
    s << std::fixed << std::setprecision(3);
    s << "q\n";  // save graphics state
    s << mmToPt(w_mm) << " 0 0 " << mmToPt(h_mm) << " "
      << mmToPt(x_mm) << " " << yToPdf(y_mm + h_mm) << " cm\n";
    s << imgName << " Do\n";
    s << "Q\n";  // restore graphics state

    // Store image data for later serialization
    // Use a simple struct stored alongside pages
    struct ImgData { int objId; std::vector<uint8_t> data; int w, h; };
    // We store this in the page's image list — but we need the actual bytes.
    // For simplicity, we'll use a member map:
    imageData_[imgObjId] = { jpegData, imgWidth, imgHeight };
}

// PDF encoding logic

std::string PdfWriter::buildFontResources(int pageIdx) {
    std::ostringstream fs;
    fs << "/Font << ";
    for (int i = 0; i < (int)fontOrder_.size(); i++) {
        fs << "/F" << i << " " << fontObjects_[fontOrder_[i]] << " 0 R ";
    }
    fs << ">>";

    // Add image XObjects for this page
    if (!pages_[pageIdx].images.empty()) {
        fs << " /XObject << ";
        for (auto& img : pages_[pageIdx].images) {
            fs << img.name << " " << img.objectId << " 0 R ";
        }
        fs << ">>";
    }

    return fs.str();
}

std::string PdfWriter::buildPdf() {
    if (pages_.empty()) newPage();

    struct ObjEntry {
        int id;
        std::string data;
        size_t offset = 0;
    };
    std::vector<ObjEntry> objects;

    auto addObj = [&](int id, const std::string& content) {
        objects.push_back({ id, content, 0 });
    };

    // Reserve IDs: 1=catalog, 2=pages
    int catalogId = 1;
    int pagesId   = 2;
    // Font objects start at 3
    // Then page objects, then content streams, then images

    // Reassign all IDs sequentially
    int nextId = 3;

    // Fonts
    std::map<std::string, int> fontIds;
    for (auto& fn : fontOrder_) {
        fontIds[fn] = nextId++;
    }

    // Pages + content streams
    struct PageIds { int pageId; int contentId; };
    std::vector<PageIds> pageIds;
    for (int i = 0; i < (int)pages_.size(); i++) {
        int pid = nextId++;
        int cid = nextId++;
        pageIds.push_back({ pid, cid });
    }

    // Image objects
    std::map<int, int> imageObjRemap;  // old ID → new ID
    for (auto& page : pages_) {
        for (auto& img : page.images) {
            int newId = nextId++;
            imageObjRemap[img.objectId] = newId;
        }
    }

    // Now build all objects

    // 1. Catalog
    {
        std::ostringstream o;
        o << "<< /Type /Catalog /Pages " << pagesId << " 0 R >>";
        addObj(catalogId, o.str());
    }

    // 2. Pages
    {
        std::ostringstream o;
        o << "<< /Type /Pages /Kids [";
        for (auto& pi : pageIds) o << pi.pageId << " 0 R ";
        o << "] /Count " << pages_.size() << " >>";
        addObj(pagesId, o.str());
    }

    // 3. Font objects
    for (auto& fn : fontOrder_) {
        std::ostringstream o;
        o << "<< /Type /Font /Subtype /Type1 /BaseFont /" << fn << " "
          << "/Encoding /WinAnsiEncoding >>";
        addObj(fontIds[fn], o.str());
    }

    // 4. Page + content stream objects
    for (int i = 0; i < (int)pages_.size(); i++) {
        std::string content = pages_[i].contentStream.str();

        // Build resource dictionary for this page
        std::ostringstream res;
        res << "/Font << ";
        for (int fi = 0; fi < (int)fontOrder_.size(); fi++) {
            res << "/F" << fi << " " << fontIds[fontOrder_[fi]] << " 0 R ";
        }
        res << ">>";

        if (!pages_[i].images.empty()) {
            res << " /XObject << ";
            for (auto& img : pages_[i].images) {
                int newId = imageObjRemap[img.objectId];
                res << img.name << " " << newId << " 0 R ";
            }
            res << ">>";
        }

        // Page object
        {
            std::ostringstream o;
            o << "<< /Type /Page /Parent " << pagesId << " 0 R "
              << "/MediaBox [0 0 " << std::fixed << std::setprecision(2)
              << mmToPt(pageW_) << " " << mmToPt(pageH_) << "] "
              << "/Resources << " << res.str() << " >> "
              << "/Contents " << pageIds[i].contentId << " 0 R >>";
            addObj(pageIds[i].pageId, o.str());
        }

        // Content stream
        {
            std::ostringstream o;
            o << "<< /Length " << content.size() << " >>\nstream\n"
              << content << "\nendstream";
            addObj(pageIds[i].contentId, o.str());
        }
    }

    // 5. Image XObjects
    for (auto& page : pages_) {
        for (auto& img : page.images) {
            auto it = imageData_.find(img.objectId);
            if (it == imageData_.end()) continue;
            int newId = imageObjRemap[img.objectId];
            auto& idata = it->second;

            std::ostringstream o;
            o << "<< /Type /XObject /Subtype /Image "
              << "/Width " << idata.width << " /Height " << idata.height << " "
              << "/ColorSpace /DeviceRGB /BitsPerComponent 8 "
              << "/Filter /DCTDecode "
              << "/Length " << idata.data.size() << " >>\nstream\n";
            // Binary data will be appended separately
            std::string header = o.str();

            // We need to handle binary data specially
            std::string objData = header;
            objData.append(reinterpret_cast<const char*>(idata.data.data()), idata.data.size());
            objData += "\nendstream";
            addObj(newId, objData);
        }
    }

    // Assemble stream
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    // Binary comment to mark as binary file
    pdf << "%\xE2\xE3\xCF\xD3\n";

    // Sort objects by ID
    std::sort(objects.begin(), objects.end(),
              [](const ObjEntry& a, const ObjEntry& b) { return a.id < b.id; });

    // Write objects and record offsets
    for (auto& obj : objects) {
        obj.offset = static_cast<size_t>(pdf.tellp());
        pdf << obj.id << " 0 obj\n" << obj.data << "\nendobj\n\n";
    }

    // Cross-reference table
    size_t xrefOffset = static_cast<size_t>(pdf.tellp());
    pdf << "xref\n";
    pdf << "0 " << (objects.back().id + 1) << "\n";
    pdf << "0000000000 65535 f \n";

    // Build offset entries (some IDs might be missing, fill with free entries)
    int maxId = objects.back().id;
    std::map<int, size_t> offsets;
    for (auto& obj : objects) offsets[obj.id] = obj.offset;

    for (int i = 1; i <= maxId; i++) {
        auto it = offsets.find(i);
        if (it != offsets.end()) {
            pdf << std::setw(10) << std::setfill('0') << it->second << " 00000 n \n";
        } else {
            pdf << "0000000000 00000 f \n";
        }
    }

    // Trailer
    pdf << "trailer\n<< /Size " << (maxId + 1)
        << " /Root " << catalogId << " 0 R >>\n";
    pdf << "startxref\n" << xrefOffset << "\n%%EOF\n";

    return pdf.str();
}

// Saving

void PdfWriter::save(const std::string& filename) {
    std::string data = buildPdf();
    std::ofstream f(filename, std::ios::binary | std::ios::trunc);
    f.write(data.data(), data.size());
}

std::vector<uint8_t> PdfWriter::toBytes() {
    std::string data = buildPdf();
    return std::vector<uint8_t>(data.begin(), data.end());
}
