#pragma once
#include "agent.h"
#include "style_profile.h"
#include <string>

// PDF Report Composer
// Uses PdfWriter + ChartRenderer to compose a full academic PDF.

class PdfEngine {
public:
    // Generate a complete PDF report and save to outputDir.
    // Returns the full path to the generated PDF.
    std::string generateReport(const ReportContent& content,
                                const StyleProfile& style,
                                const std::string& outputDir);
};
