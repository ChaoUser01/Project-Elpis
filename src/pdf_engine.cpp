#include "pdf_engine.h"
#include "pdf_writer.h"
#include "chart_renderer.h"
#include "utils.h"

#include <sstream>
#include <algorithm>
#include <iomanip>

// Academic report generation using the custom PDF writer

std::string PdfEngine::generateReport(const ReportContent& content,
                                       const StyleProfile& style,
                                       const std::string& outputDir) {
    utils::ensureDir(outputDir);

    PdfWriter pdf(210.0f, 297.0f);  // A4
    pdf.setMargins(25.0f, 20.0f, 25.0f, 20.0f);

    float leftM  = pdf.marginLeft();
    float topM   = pdf.marginTop();
    float cw     = pdf.contentWidth();
    float bottom = pdf.contentBottom();

    // Cover page
    pdf.newPage();
    float y = 70.0f;

    // Top decorative line
    pdf.setColor(style.accentR, style.accentG, style.accentB);
    pdf.drawLine(leftM, 40.0f, leftM + cw, 40.0f, 2.0f);

    if (style.borderStyle == StyleProfile::BorderStyle::Double) {
        pdf.drawLine(leftM, 42.0f, leftM + cw, 42.0f, 0.5f);
    }

    // Title
    pdf.setFont(style.headingFont, style.titleFontSize);
    pdf.setTextColor(style.accentR, style.accentG, style.accentB);

    // Word-wrap the title centered
    float titleLineH = style.titleFontSize * 1.3f / 2.83465f;
    std::string titleText = content.title;
    // Simple centering: draw wrapped and use approximate centering
    {
        // Split title into wrapped lines
        std::vector<std::string> titleLines;
        std::string line;
        float lineW = 0;
        float spW = pdf.stringWidth(" ", style.headingFont, style.titleFontSize);

        for (auto& word : utils::split(titleText, ' ')) {
            float ww = pdf.stringWidth(word, style.headingFont, style.titleFontSize);
            if (lineW > 0 && lineW + spW + ww > cw * 0.8f) {
                titleLines.push_back(line);
                line = word;
                lineW = ww;
            } else {
                if (!line.empty()) { line += " "; lineW += spW; }
                line += word;
                lineW += ww;
            }
        }
        if (!line.empty()) titleLines.push_back(line);

        for (auto& tl : titleLines) {
            float tw = pdf.stringWidth(tl, style.headingFont, style.titleFontSize);
            pdf.drawText(leftM + (cw - tw) / 2, y, tl);
            y += titleLineH;
        }
    }

    y += 5;

    // Subtitle
    if (!content.subtitle.empty()) {
        pdf.setFont(style.bodyFontItalic, style.bodyFontSize + 2);
        pdf.setTextColor(0.35f, 0.35f, 0.35f);
        float sw = pdf.stringWidth(content.subtitle);
        pdf.drawText(leftM + (cw - sw) / 2, y, content.subtitle);
        y += 12;
    }

    // Decorative rule under title
    pdf.setColor(style.accentR, style.accentG, style.accentB);
    float ruleW = cw * 0.4f;
    pdf.drawLine(leftM + (cw - ruleW) / 2, y, leftM + (cw + ruleW) / 2, y, 1.0f);
    y += 15;

    // Author
    pdf.setFont(style.bodyFont, style.bodyFontSize + 1);
    pdf.setTextColor(0.15f, 0.15f, 0.15f);
    float aw = pdf.stringWidth(content.author);
    pdf.drawText(leftM + (cw - aw) / 2, y, content.author);
    y += 8;

    // Institution
    if (!content.institution.empty()) {
        pdf.setFont(style.bodyFontItalic, style.bodyFontSize);
        pdf.setTextColor(0.3f, 0.3f, 0.3f);
        float iw = pdf.stringWidth(content.institution);
        pdf.drawText(leftM + (cw - iw) / 2, y, content.institution);
        y += 8;
    }

    // Date
    pdf.setFont(style.bodyFont, style.bodyFontSize);
    pdf.setTextColor(0.3f, 0.3f, 0.3f);
    float dw = pdf.stringWidth(content.date);
    pdf.drawText(leftM + (cw - dw) / 2, y, content.date);
    y += 8;

    // Discipline badge
    if (!content.discipline.empty()) {
        pdf.setFont(style.bodyFont, style.bodyFontSize - 1);
        pdf.setTextColor(style.accentR, style.accentG, style.accentB);
        std::string badge = "[ " + content.discipline + " ]";
        float bw = pdf.stringWidth(badge);
        pdf.drawText(leftM + (cw - bw) / 2, y + 5, badge);
    }

    // Bottom decorative line on cover
    pdf.setColor(style.accentR, style.accentG, style.accentB);
    pdf.drawLine(leftM, 260.0f, leftM + cw, 260.0f, 2.0f);
    if (style.borderStyle == StyleProfile::BorderStyle::Double) {
        pdf.drawLine(leftM, 258.0f, leftM + cw, 258.0f, 0.5f);
    }

    // ── Abstract page ──────────────────────────────────────────────────────
    if (!content.abstractText.empty()) {
        pdf.newPage();
        y = topM;

        // "Abstract" heading
        pdf.setFont(style.headingFont, style.headingFontSize);
        pdf.setTextColor(style.accentR, style.accentG, style.accentB);
        std::string absTitle = "Abstract";
        float atw = pdf.stringWidth(absTitle);
        pdf.drawText(leftM + (cw - atw) / 2, y, absTitle);
        y += 10;

        // Rule under abstract heading
        pdf.setColor(style.accentR, style.accentG, style.accentB);
        pdf.drawLine(leftM + cw * 0.2f, y, leftM + cw * 0.8f, y, 0.5f);
        y += 6;

        // Abstract text (slightly indented)
        float indent = 5.0f;
        pdf.setFont(style.bodyFontItalic, style.bodyFontSize);
        pdf.setTextColor(0.15f, 0.15f, 0.15f);
        y = pdf.drawWrappedText(leftM + indent, y, cw - 2 * indent,
                                 content.abstractText, 1.5f);
    }

    // ── Body sections ──────────────────────────────────────────────────────
    auto renderHeading = [&](const std::string& heading, float& curY) {
        // Check if enough space for heading + at least 3 lines
        float needed = 15.0f + style.bodyFontSize * 1.4f / 2.83465f * 3;
        if (curY + needed > bottom) {
            pdf.newPage();
            curY = topM;
        }

        curY += 6; // space before heading

        pdf.setFont(style.headingFont, style.headingFontSize);
        pdf.setTextColor(style.accentR, style.accentG, style.accentB);

        switch (style.headingStyle) {
            case StyleProfile::HeadingStyle::Underline:
                pdf.drawText(leftM, curY, heading);
                curY += style.headingFontSize * 1.2f / 2.83465f;
                pdf.setColor(style.accentR, style.accentG, style.accentB);
                pdf.drawLine(leftM, curY, leftM + pdf.stringWidth(heading, style.headingFont, style.headingFontSize) + 2, curY, 0.75f);
                curY += 4;
                break;

            case StyleProfile::HeadingStyle::Rule:
                pdf.setColor(style.accentR, style.accentG, style.accentB);
                pdf.drawLine(leftM, curY, leftM + cw, curY, 0.5f);
                curY += 3;
                pdf.drawText(leftM, curY, heading);
                curY += style.headingFontSize * 1.2f / 2.83465f + 2;
                break;

            case StyleProfile::HeadingStyle::BoxedRule:
                pdf.setColor(style.accentR, style.accentG, style.accentB);
                {
                    float hw = pdf.stringWidth(heading, style.headingFont, style.headingFontSize);
                    float bh = style.headingFontSize * 1.4f / 2.83465f;
                    // Light accent background
                    float lr = std::min(1.0f, style.accentR + 0.7f);
                    float lg = std::min(1.0f, style.accentG + 0.7f);
                    float lb = std::min(1.0f, style.accentB + 0.7f);
                    pdf.setColor(lr, lg, lb);
                    pdf.drawRect(leftM, curY - 1, cw, bh + 2, true);
                    pdf.setColor(style.accentR, style.accentG, style.accentB);
                    pdf.drawRect(leftM, curY - 1, cw, bh + 2, false, 0.5f);
                }
                pdf.drawText(leftM + 3, curY + 1, heading);
                curY += style.headingFontSize * 1.4f / 2.83465f + 4;
                break;

            case StyleProfile::HeadingStyle::SmallCaps:
            default: {
                // Convert to uppercase for small-caps effect
                std::string upper = heading;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                pdf.setFont(style.headingFont, style.headingFontSize - 1);
                pdf.drawText(leftM, curY, upper);
                curY += style.headingFontSize * 1.2f / 2.83465f + 3;
                break;
            }
        }
    };

    // Start body content
    pdf.newPage();
    y = topM;

    for (auto& section : content.sections) {
        // Heading
        renderHeading(section.heading, y);

        // Body text
        pdf.setFont(style.bodyFont, style.bodyFontSize);
        pdf.setTextColor(0.1f, 0.1f, 0.1f);
        y = pdf.drawWrappedText(leftM, y, cw, section.body, 1.45f);
        y += 3;

        // Charts
        for (auto& chart : section.charts) {
            if (chart.type == ChartData::Type::Table) {
                float tableH = 6.5f * ((float)chart.rows.size() + 2) + 10;
                if (y + tableH > bottom) {
                    pdf.newPage();
                    y = topM;
                }
                y = ChartRenderer::drawTable(pdf, chart, leftM, y, cw,
                                              style.accentR, style.accentG, style.accentB);
            } else {
                float chartH = 60.0f;
                if (y + chartH > bottom) {
                    pdf.newPage();
                    y = topM;
                }
                y = ChartRenderer::drawChart(pdf, chart, leftM, y, cw, chartH,
                                              style.accentR, style.accentG, style.accentB);
            }
            y += 3;
        }

        y += 2;
    }

    // ── References ─────────────────────────────────────────────────────────
    if (!content.references.empty()) {
        renderHeading("References", y);

        pdf.setFont(style.bodyFont, style.bodyFontSize - 1);
        pdf.setTextColor(0.15f, 0.15f, 0.15f);

        for (auto& ref : content.references) {
            y = pdf.drawWrappedText(leftM + 5, y, cw - 10, ref, 1.35f);
            y += 1.5f;
        }
    }

    // ── Page numbers ───────────────────────────────────────────────────────
    // We go back and add page numbers by creating a new content overlay.
    // Since our PdfWriter writes sequentially, we add page numbers during
    // the content generation. For simplicity, we'll skip retroactive numbering
    // and note that in a production system, a second pass would be used.
    // Instead, we add a footer note on the last page:
    int totalPages = pdf.pageCount();
    // We can't modify previous pages easily, so this is a known limitation
    // of this simple implementation.

    // ── Save ───────────────────────────────────────────────────────────────
    std::string filename = utils::generateFilename("report", "pdf");
    std::string fullPath = outputDir + "/" + filename;
    pdf.save(fullPath);

    return fullPath;
}
