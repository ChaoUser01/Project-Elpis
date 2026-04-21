#pragma once
#include "pdf_writer.h"
#include <string>
#include <vector>
#include <map>

// ── Chart types that can be drawn directly into a PDF ──────────────────────

struct ChartData {
    enum class Type { Bar, Line, Pie, Table };
    Type type = Type::Bar;
    std::string title;
    std::vector<std::string> labels;
    std::vector<float> values;
    std::string xAxisLabel;
    std::string yAxisLabel;

    // For tables
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

class ChartRenderer {
public:
    // Draw a chart into the PDF at the given position.
    // Returns the Y position after the chart (mm from top).
    static float drawChart(PdfWriter& pdf, const ChartData& chart,
                           float x_mm, float y_mm, float w_mm, float h_mm,
                           float accentR = 0.15f, float accentG = 0.25f, float accentB = 0.45f);

    // Draw a data table into the PDF.
    static float drawTable(PdfWriter& pdf, const ChartData& chart,
                           float x_mm, float y_mm, float w_mm,
                           float accentR = 0.15f, float accentG = 0.25f, float accentB = 0.45f);

private:
    static float drawBarChart(PdfWriter& pdf, const ChartData& chart,
                              float x, float y, float w, float h,
                              float ar, float ag, float ab);
    static float drawLineChart(PdfWriter& pdf, const ChartData& chart,
                               float x, float y, float w, float h,
                               float ar, float ag, float ab);
    static float drawPieChart(PdfWriter& pdf, const ChartData& chart,
                              float x, float y, float w, float h,
                              float ar, float ag, float ab);

    // Academic colour palette (8 muted tones)
    struct Color { float r, g, b; };
    static Color getPaletteColor(int index);
};
