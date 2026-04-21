#pragma once
#include <string>
#include <vector>
#include "chart_renderer.h"

// Content structures

struct CodeBlock {
    std::string filename;
    std::string language;
    std::string code;
};

struct Section {
    std::string heading;
    std::string body;
    std::vector<CodeBlock> codeBlocks;
    std::vector<ChartData> charts;
};

struct ReportContent {
    std::string title;
    std::string subtitle;
    std::string author;
    std::string date;
    std::string institution;
    std::string abstractText;
    std::vector<Section> sections;
    std::vector<std::string> references;
    std::string discipline;   // e.g. "Computer Science", "Engineering"
};

// Agent handles prompt analysis and content generation

class Agent {
public:
    Agent();

    // Set the API key. If empty, uses demo mode.
    void setApiKey(const std::string& key) { apiKey_ = key; }
    bool hasApiKey() const { return !apiKey_.empty(); }

    // Explicit provider config (overrides auto-detection)
    void setProvider(const std::string& provider) { provider_ = provider; }
    void setBaseUrl(const std::string& url) { baseUrl_ = url; }
    void setModel(const std::string& model) { model_ = model; }

    // Main pipeline: prompt → structured report content.
    ReportContent generate(const std::string& prompt);

private:
    std::string apiKey_;
    std::string provider_;   // "groq", "gemini", "openai", or empty (auto-detect)
    std::string baseUrl_;    // explicit override
    std::string model_;      // explicit override

    // Resolves the actual URL and model to use based on provider/key
    void resolveEndpoint(std::string& outUrl, std::string& outModel);

    // LLM-based generation
    ReportContent generateWithLLM(const std::string& prompt);

    // Demo mode: returns rich sample content based on prompt keywords
    ReportContent generateDemo(const std::string& prompt);

    // Detect discipline from prompt
    std::string detectDiscipline(const std::string& prompt);
};
