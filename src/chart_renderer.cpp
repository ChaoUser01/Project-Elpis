#include "chart_renderer.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Palette ────────────────────────────────────────────────────────────────

ChartRenderer::Color ChartRenderer::getPaletteColor(int index) {
    static const Color palette[] = {
        { 0.20f, 0.40f, 0.65f },  // steel blue
        { 0.75f, 0.30f, 0.25f },  // brick red
        { 0.30f, 0.60f, 0.35f },  // sage green
        { 0.55f, 0.40f, 0.65f },  // muted purple
        { 0.85f, 0.55f, 0.20f },  // amber
        { 0.35f, 0.55f, 0.55f },  // teal
        { 0.65f, 0.35f, 0.45f },  // mauve
        { 0.45f, 0.50f, 0.35f },  // olive
    };
    return palette[index % 8];
}

// ── Bar chart ──────────────────────────────────────────────────────────────

float ChartRenderer::drawBarChart(PdfWriter& pdf, const ChartData& chart,
                                   float x, float y, float w, float h,
                                   float ar, float ag, float ab) {
    if (chart.values.empty()) return y + h;

    float maxVal = *std::max_element(chart.values.begin(), chart.values.end());
    if (maxVal <= 0) maxVal = 1;

    float chartLeft   = x + 15;   // space for Y-axis labels
    float chartBottom = y + h - 10; // space for X-axis labels
    float chartTop    = y + 8;     // space for title
    float chartRight  = x + w - 5;
    float chartW = chartRight - chartLeft;
    float chartH = chartBottom - chartTop;

    // Title
    pdf.setFont("Helvetica-Bold", 9);
    pdf.setTextColor(0.1f, 0.1f, 0.1f);
    float tw = pdf.stringWidth(chart.title);
    pdf.drawText(x + (w - tw) / 2, y + 2, chart.title);

    // Y-axis gridlines and labels
    pdf.setFont("Helvetica", 6);
    int gridLines = 5;
    for (int i = 0; i <= gridLines; i++) {
        float frac = (float)i / gridLines;
        float gy = chartBottom - frac * chartH;
        float val = frac * maxVal;

        // Gridline
        pdf.setColor(0.85f, 0.85f, 0.85f);
        pdf.drawLine(chartLeft, gy, chartRight, gy, 0.3f);

        // Label
        std::ostringstream lbl;
        lbl << std::fixed << std::setprecision(1) << val;
        pdf.setTextColor(0.4f, 0.4f, 0.4f);
        pdf.drawText(x + 1, gy - 1, lbl.str());
    }

    // Bars
    int n = (int)chart.values.size();
    float barGap = 2;
    float barW = (chartW - barGap * (n + 1)) / n;

    for (int i = 0; i < n; i++) {
        float bx = chartLeft + barGap + i * (barW + barGap);
        float bh = (chart.values[i] / maxVal) * chartH;
        float by = chartBottom - bh;

        auto col = getPaletteColor(i);
        pdf.setColor(col.r, col.g, col.b);
        pdf.drawRect(bx, by, barW, bh, true);

        // Label
        if (i < (int)chart.labels.size()) {
            pdf.setFont("Helvetica", 5.5f);
            pdf.setTextColor(0.3f, 0.3f, 0.3f);
            std::string lbl = chart.labels[i];
            if (lbl.size() > 8) lbl = lbl.substr(0, 7) + ".";
            float lw = pdf.stringWidth(lbl);
            pdf.drawText(bx + (barW - lw) / 2, chartBottom + 2, lbl);
        }
    }

    // Axes
    pdf.setColor(0.3f, 0.3f, 0.3f);
    pdf.drawLine(chartLeft, chartTop, chartLeft, chartBottom, 0.5f);
    pdf.drawLine(chartLeft, chartBottom, chartRight, chartBottom, 0.5f);

    return y + h + 3;
}

// ── Line chart ─────────────────────────────────────────────────────────────

float ChartRenderer::drawLineChart(PdfWriter& pdf, const ChartData& chart,
                                    float x, float y, float w, float h,
                                    float ar, float ag, float ab) {
    if (chart.values.empty()) return y + h;

    float maxVal = *std::max_element(chart.values.begin(), chart.values.end());
    float minVal = *std::min_element(chart.values.begin(), chart.values.end());
    float range = maxVal - minVal;
    if (range <= 0) range = 1;

    float chartLeft   = x + 15;
    float chartBottom = y + h - 10;
    float chartTop    = y + 8;
    float chartRight  = x + w - 5;
    float chartW = chartRight - chartLeft;
    float chartH = chartBottom - chartTop;

    // Title
    pdf.setFont("Helvetica-Bold", 9);
    pdf.setTextColor(0.1f, 0.1f, 0.1f);
    float tw = pdf.stringWidth(chart.title);
    pdf.drawText(x + (w - tw) / 2, y + 2, chart.title);

    // Grid
    pdf.setFont("Helvetica", 6);
    for (int i = 0; i <= 5; i++) {
        float frac = (float)i / 5;
        float gy = chartBottom - frac * chartH;
        pdf.setColor(0.88f, 0.88f, 0.88f);
        pdf.drawLine(chartLeft, gy, chartRight, gy, 0.25f);

        std::ostringstream lbl;
        lbl << std::fixed << std::setprecision(1) << (minVal + frac * range);
        pdf.setTextColor(0.4f, 0.4f, 0.4f);
        pdf.drawText(x + 1, gy - 1, lbl.str());
    }

    // Line segments
    int n = (int)chart.values.size();
    pdf.setColor(ar, ag, ab);
    for (int i = 0; i < n - 1; i++) {
        float x1 = chartLeft + (float)i / (n - 1) * chartW;
        float y1 = chartBottom - ((chart.values[i] - minVal) / range) * chartH;
        float x2 = chartLeft + (float)(i + 1) / (n - 1) * chartW;
        float y2 = chartBottom - ((chart.values[i + 1] - minVal) / range) * chartH;
        pdf.drawLine(x1, y1, x2, y2, 1.0f);
    }

    // Data points (small filled circles approximated as tiny rects)
    for (int i = 0; i < n; i++) {
        float px = chartLeft + (float)i / std::max(1, n - 1) * chartW;
        float py = chartBottom - ((chart.values[i] - minVal) / range) * chartH;
        pdf.drawRect(px - 0.8f, py - 0.8f, 1.6f, 1.6f, true);
    }

    // Axes
    pdf.setColor(0.3f, 0.3f, 0.3f);
    pdf.drawLine(chartLeft, chartTop, chartLeft, chartBottom, 0.5f);
    pdf.drawLine(chartLeft, chartBottom, chartRight, chartBottom, 0.5f);

    return y + h + 3;
}

// ── Pie chart ──────────────────────────────────────────────────────────────

float ChartRenderer::drawPieChart(PdfWriter& pdf, const ChartData& chart,
                                   float x, float y, float w, float h,
                                   float ar, float ag, float ab) {
    if (chart.values.empty()) return y + h;

    // Title
    pdf.setFont("Helvetica-Bold", 9);
    pdf.setTextColor(0.1f, 0.1f, 0.1f);
    float tw = pdf.stringWidth(chart.title);
    pdf.drawText(x + (w - tw) / 2, y + 2, chart.title);

    // For a true pie chart we'd need arc drawing (PDF Bezier curves).
    // We approximate with a legend + proportional bars instead.
    float total = 0;
    for (float v : chart.values) total += v;
    if (total <= 0) total = 1;

    float ly = y + 12;
    int n = (int)chart.values.size();
    float barFullW = w - 30;

    for (int i = 0; i < n; i++) {
        float pct = chart.values[i] / total * 100.0f;
        auto col = getPaletteColor(i);

        // Coloured rectangle
        pdf.setColor(col.r, col.g, col.b);
        float bw = (chart.values[i] / total) * barFullW;
        pdf.drawRect(x + 25, ly, std::max(bw, 1.0f), 4, true);

        // Label
        std::string lbl = (i < (int)chart.labels.size()) ? chart.labels[i] : "Item";
        std::ostringstream txt;
        txt << lbl << " (" << std::fixed << std::setprecision(1) << pct << "%)";
        pdf.setFont("Helvetica", 6.5f);
        pdf.setTextColor(0.2f, 0.2f, 0.2f);
        pdf.drawText(x + 2, ly + 0.5f, txt.str());

        ly += 7;
    }

    return std::max(y + h, ly) + 3;
}

// ── Table ──────────────────────────────────────────────────────────────────

float ChartRenderer::drawTable(PdfWriter& pdf, const ChartData& chart,
                                float x, float y, float w,
                                float ar, float ag, float ab) {
    if (chart.headers.empty()) return y;

    int cols = (int)chart.headers.size();
    float colW = w / cols;
    float rowH = 6.5f;
    float curY = y;

    // Title
    if (!chart.title.empty()) {
        pdf.setFont("Helvetica-Bold", 9);
        pdf.setTextColor(0.1f, 0.1f, 0.1f);
        float tw = pdf.stringWidth(chart.title);
        pdf.drawText(x + (w - tw) / 2, curY, chart.title);
        curY += 6;
    }

    // Header row background
    pdf.setColor(ar, ag, ab);
    pdf.drawRect(x, curY, w, rowH, true);

    // Header text
    pdf.setFont("Helvetica-Bold", 7);
    pdf.setTextColor(1, 1, 1);
    for (int c = 0; c < cols; c++) {
        pdf.drawText(x + c * colW + 1.5f, curY + 1.5f, chart.headers[c]);
    }
    curY += rowH;

    // Data rows
    pdf.setFont("Helvetica", 7);
    for (int r = 0; r < (int)chart.rows.size(); r++) {
        // Alternating row background
        if (r % 2 == 0) {
            pdf.setColor(0.95f, 0.95f, 0.97f);
            pdf.drawRect(x, curY, w, rowH, true);
        }

        // Row border
        pdf.setColor(0.8f, 0.8f, 0.8f);
        pdf.drawLine(x, curY + rowH, x + w, curY + rowH, 0.25f);

        pdf.setTextColor(0.15f, 0.15f, 0.15f);
        for (int c = 0; c < cols && c < (int)chart.rows[r].size(); c++) {
            pdf.drawText(x + c * colW + 1.5f, curY + 1.5f, chart.rows[r][c]);
        }
        curY += rowH;
    }

    // Outer border
    float tableH = curY - y;
    pdf.setColor(ar, ag, ab);
    pdf.drawRect(x, y + (chart.title.empty() ? 0 : 6), w,
                 tableH - (chart.title.empty() ? 0 : 6), false, 0.5f);

    return curY + 3;
}

// ── Dispatcher ─────────────────────────────────────────────────────────────

float ChartRenderer::drawChart(PdfWriter& pdf, const ChartData& chart,
                                float x, float y, float w, float h,
                                float ar, float ag, float ab) {
    switch (chart.type) {
        case ChartData::Type::Bar:
            return drawBarChart(pdf, chart, x, y, w, h, ar, ag, ab);
        case ChartData::Type::Line:
            return drawLineChart(pdf, chart, x, y, w, h, ar, ag, ab);
        case ChartData::Type::Pie:
            return drawPieChart(pdf, chart, x, y, w, h, ar, ag, ab);
        case ChartData::Type::Table:
            return drawTable(pdf, chart, x, y, w, ar, ag, ab);
    }
    return y + h;
}
