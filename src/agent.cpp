#include "agent.h"
#include "http_client.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <array>
#include <set>

using json = nlohmann::json;

static bool containsAny(const std::string& haystackLower, const std::initializer_list<const char*>& needlesLower) {
    for (const char* n : needlesLower) {
        if (haystackLower.find(n) != std::string::npos) return true;
    }
    return false;
}

static size_t approximateWordCount(const std::string& s) {
    size_t words = 0;
    bool inWord = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            if (!inWord) { inWord = true; words++; }
        } else {
            inWord = false;
        }
    }
    return words;
}

Agent::Agent() {
    // No longer auto-load from env — keys come from BYOK login
}

// Provider resolution logic

void Agent::resolveEndpoint(std::string& outUrl, std::string& outModel) {
    // If explicit overrides are set, use them
    if (!baseUrl_.empty() && !model_.empty()) {
        outUrl = baseUrl_;
        outModel = model_;
        return;
    }

    // Auto-detect provider from API key prefix
    std::string provider = provider_;
    if (provider.empty() && !apiKey_.empty()) {
        std::string key = utils::trim(apiKey_);
        if (key.size() >= 4 && key.substr(0, 4) == "gsk_") {
            provider = "groq";
        } else if (key.size() >= 4 && key.substr(0, 4) == "AIza") {
            provider = "gemini";
        } else if (key.size() >= 3 && key.substr(0, 3) == "sk-") {
            provider = "openai";
        } else {
            // Fallback: check env vars
            const char* envUrl = std::getenv("LLM_BASE_URL");
            if (envUrl && envUrl[0] != '\0') {
                outUrl = envUrl;
                const char* envModel = std::getenv("LLM_MODEL");
                outModel = (envModel && envModel[0] != '\0') ? envModel : "gpt-4o";
                return;
            }
            provider = "openai";
        }
    }

    if (provider == "groq") {
        outUrl   = "https://api.groq.com/openai/v1/chat/completions";
        outModel = model_.empty() ? "qwen/qwen3-32b" : model_; 
    } else if (provider == "gemini") {
        outUrl   = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
        outModel = model_.empty() ? "gemini-2.5-flash" : model_;
    } else {
        outUrl   = "https://api.openai.com/v1/chat/completions";
        outModel = model_.empty() ? "gpt-4o" : model_;
    }
}


// Detect research discipline from prompt keywords

std::string Agent::detectDiscipline(const std::string& prompt) {
    std::string lower = utils::toLower(prompt);

    struct KW { const char* keyword; const char* discipline; };
    static const KW keywords[] = {
        {"machine learning",  "Computer Science"},
        {"neural network",    "Computer Science"},
        {"algorithm",         "Computer Science"},
        {"software",          "Computer Science"},
        {"programming",       "Computer Science"},
        {"database",          "Computer Science"},
        {"python",            "Computer Science"},
        {"java",              "Computer Science"},
        {"regression",        "Data Science"},
        {"classification",    "Data Science"},
        {"statistics",        "Data Science"},
        {"data analysis",     "Data Science"},
        {"circuit",           "Electrical Engineering"},
        {"voltage",           "Electrical Engineering"},
        {"transistor",        "Electrical Engineering"},
        {"signal processing", "Electrical Engineering"},
        {"thermodynamic",     "Mechanical Engineering"},
        {"fluid",             "Mechanical Engineering"},
        {"structural",        "Civil Engineering"},
        {"concrete",          "Civil Engineering"},
        {"chemical reaction", "Chemistry"},
        {"molecular",         "Chemistry"},
        {"organic",           "Chemistry"},
        {"quantum",           "Physics"},
        {"relativity",        "Physics"},
        {"kinematic",         "Physics"},
        {"cell biology",      "Biology"},
        {"genetics",          "Biology"},
        {"evolution",         "Biology"},
        {"essay",             "Humanities"},
        {"philosophy",        "Humanities"},
        {"literature",        "Humanities"},
        {"history",           "Humanities"},
        {"sociolog",          "Social Sciences"},
        {"psychology",        "Social Sciences"},
        {"economic",          "Economics"},
        {"finance",           "Economics"},
        {"mathematics",       "Mathematics"},
        {"calculus",          "Mathematics"},
        {"linear algebra",    "Mathematics"},
    };

    for (auto& kw : keywords) {
        if (lower.find(kw.keyword) != std::string::npos)
            return kw.discipline;
    }
    return "General Academic";
}

bool Agent::topicRequiresCode(const std::string& prompt) const {
    const std::string lower = utils::toLower(prompt);
    static const std::array<const char*, 17> implementationSignals = {{
        "code", "implement", "implementation", "build ", "develop", "script",
        "program", "prototype", "simulation", "dataset", "train a model",
        "api", "application", "dashboard", "workflow", "automation", "algorithm"
    }};

    static const std::array<const char*, 12> conceptSignals = {{
        "implication", "implications", "ethics", "ethical", "society",
        "social", "policy", "history", "overview", "literature review",
        "essay", "discussion"
    }};

    bool hasImplementationSignal = std::any_of(
        implementationSignals.begin(), implementationSignals.end(),
        [&](const char* token) { return lower.find(token) != std::string::npos; });

    bool hasConceptSignal = std::any_of(
        conceptSignals.begin(), conceptSignals.end(),
        [&](const char* token) { return lower.find(token) != std::string::npos; });

    if (hasConceptSignal && !hasImplementationSignal) {
        return false;
    }

    return hasImplementationSignal;
}

bool Agent::isRenderedHtmlAcceptable(const std::string& html) {
    if (html.size() < 200) return false;
    // Hard safety gates: no scripts.
    const std::string lower = utils::toLower(html);
    if (containsAny(lower, {"<script", "javascript:"})) {
        return false;
    }
    // Block remote resources (but allow https links in text/references).
    if (containsAny(lower, {"src=\"http://", "src=\"https://", "href=\"http://", "href=\"https://"})) {
        return false;
    }
    // Must look like a full document.
    if (lower.find("<html") == std::string::npos || lower.find("<body") == std::string::npos) {
        return false;
    }
    return true;
}

void Agent::normalizeAssets(ReportContent& content, bool wantsCodeAssets) {
    if (!wantsCodeAssets) {
        content.assets.clear();
        for (auto& section : content.sections) {
            section.codeBlocks.clear();
        }
        return;
    }

    std::vector<CodeBlock> mergedAssets = content.assets;
    for (auto& section : content.sections) {
        for (auto& block : section.codeBlocks) {
            if (!block.code.empty()) {
                mergedAssets.push_back(block);
            }
        }
        section.codeBlocks.clear();
    }

    std::vector<CodeBlock> normalized;
    std::set<std::string> seen;
    bool hasGuide = false;

    for (const auto& asset : mergedAssets) {
        if (asset.code.empty()) {
            continue;
        }

        CodeBlock clean = asset;
        clean.filename = utils::trim(clean.filename);
        if (clean.filename.empty()) {
            clean.filename = "artifact";
        }

        const std::string key = utils::toLower(clean.filename + "::" + clean.language);
        if (!seen.insert(key).second) {
            continue;
        }

        const std::string lowerName = utils::toLower(clean.filename);
        if (lowerName == "readme" || lowerName == "readme.md" || lowerName == "setup.txt" || lowerName == "requirements.txt") {
            hasGuide = true;
        }

        normalized.push_back(clean);
    }

    if (!normalized.empty() && !hasGuide) {
        CodeBlock guide;
        guide.filename = "README.md";
        guide.language = "markdown";
        guide.code =
            "# Asset Bundle\n\n"
            "Generated alongside the report.\n\n"
            "## Files\n";
        for (const auto& asset : normalized) {
            guide.code += "- `" + asset.filename + "`\n";
        }
        guide.code += "\n## Usage\nOpen the files in dependency order and adapt runtime requirements to the target environment.\n";
        normalized.push_back(guide);
    }

    content.assets = std::move(normalized);
}

Agent::IntentDecision Agent::classifyIntent(const std::string& prompt, const std::string& templateHtml) {
    IntentDecision d;

    // Hard override: if local heuristic says code is needed, do not allow the classifier to downgrade it.
    // This prevents "missing code" in clearly implementation-oriented prompts.
    const bool heuristicWantsAssets = topicRequiresCode(prompt);

    // If no API key, we can only use heuristics.
    if (!hasApiKey()) {
        d.wantsAssets = heuristicWantsAssets;
        d.reportOnly = !d.wantsAssets;
        d.confidence = 0.5;
        d.reason = "heuristic_no_key";
        return d;
    }

    // Resolve provider-specific endpoint
    std::string resolvedUrl, resolvedModel;
    resolveEndpoint(resolvedUrl, resolvedModel);

    // Trim API key (crucial for Windows .env files which may have \r)
    std::string cleanKey = utils::trim(apiKey_);

    // Lightweight classifier (cheap + strict JSON).
    // It should decide based on user intent: do they need runnable artifacts, or only a print-ready report?
    const std::string systemPrompt =
        "Return ONLY valid JSON. Decide whether this academic request requires runnable code/assets.\n"
        "You must choose one mode:\n"
        "- \"report_only\": a rigorous lab report with no runnable assets.\n"
        "- \"report_plus_assets\": include standalone runnable artifacts only if essential to execute analysis/simulation/data processing.\n\n"
        "Output schema:\n"
        "{\n"
        "  \"mode\": \"report_only\" | \"report_plus_assets\",\n"
        "  \"needs_code\": boolean,\n"
        "  \"confidence\": number,\n"
        "  \"reason\": string\n"
        "}\n\n"
        "Rules:\n"
        "- Default to report_only unless the user asked for computation, simulation, dataset processing, plotting automation, or reproducibility scripts.\n"
        "- A lab report can contain equations/derivations without needing code.\n"
        "- If a template is provided, treat it as a print-only deliverable.";

    json systemMsg = {{"role", "system"}, {"content", systemPrompt}};
    json userMsg = {{"role", "user"}, {"content", "PROMPT:\n" + prompt + "\n\nTEMPLATE_PRESENT: " + std::string(templateHtml.empty() ? "false" : "true")}};

    json reqBody = {
        {"model", resolvedModel},
        {"messages", json::array({systemMsg, userMsg})},
        {"temperature", 0.0},
        {"max_tokens", 256}
    };

    if (resolvedUrl.find("groq.com") == std::string::npos) {
        reqBody["response_format"] = {{"type", "json_object"}};
    }

    HttpClient client;
    auto resp = client.post(
        resolvedUrl,
        reqBody.dump(),
        {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + cleanKey}
        }
    );

    if (!resp.ok()) {
        d.wantsAssets = heuristicWantsAssets;
        d.reportOnly = !d.wantsAssets;
        d.confidence = 0.4;
        d.reason = "classifier_http_" + std::to_string(resp.status);
        return d;
    }

    try {
        auto j = json::parse(resp.body);
        auto text = j["choices"][0]["message"]["content"].get<std::string>();
        std::string cleanedJson = utils::extractJson(text);
        auto data = json::parse(cleanedJson);

        const std::string mode = data.value("mode", "report_only");
        const bool needsCode = data.value("needs_code", false);
        const double conf = data.value("confidence", 0.0);
        const std::string reason = data.value("reason", "");

        d.wantsAssets = (mode == "report_plus_assets") || needsCode || heuristicWantsAssets;
        d.reportOnly = !d.wantsAssets;
        d.confidence = conf;
        d.reason = reason;
        return d;
    } catch (...) {
        d.wantsAssets = heuristicWantsAssets;
        d.reportOnly = !d.wantsAssets;
        d.confidence = 0.35;
        d.reason = "classifier_parse_error";
        return d;
    }
}

// ── LLM-based generation ──────────────────────────────────────────────────

ReportContent Agent::generate(const std::string& prompt, const std::string& templateHtml, const std::string& designBrief) {
    if (!hasApiKey()) {
        std::cout << "[Agent] No API key provided, using Demo Mode." << std::endl;
        return generateDemo(prompt);
    }

    HttpClient client;
    const IntentDecision intent = classifyIntent(prompt, templateHtml);
    const bool wantsCodeAssets = intent.wantsAssets;
    const std::string codeDirective = wantsCodeAssets
        ? "MODE=report_plus_assets. Put runnable deliverables in the top-level assets array, not inside the report body. Each asset must be complete, coherent, and directly useful."
        : "MODE=report_only. assets MUST be an empty array and every section.codeBlocks MUST be empty. Do not include any runnable code files.";

    json systemMsg = {
        {"role", "system"},
        {"content",
         "You are an expert academic report agent. Produce one polished report tailored to the user's request.\n\n"
         "Return ONLY valid JSON with this schema:\n"
         "{\n"
         "  \"title\": string,\n"
         "  \"subtitle\": string,\n"
         "  \"author\": string,\n"
         "  \"institution\": string,\n"
         "  \"discipline\": string,\n"
         "  \"abstract\": string,\n"
         "  \"sections\": [{\n"
         "     \"heading\": string,\n"
         "     \"body\": string,\n"
         "     \"codeBlocks\": [{\"filename\": string, \"language\": string, \"code\": string}],\n"
         "     \"charts\": [{\n"
         "        \"title\": string,\n"
         "        \"type\": \"bar\"|\"line\"|\"pie\"|\"table\",\n"
         "        \"labels\": [string],\n"
         "        \"values\": [number],\n"
         "        \"headers\": [string],\n"
         "        \"rows\": [[string]]\n"
         "     }]\n"
         "  }],\n"
         "  \"references\": [string],\n"
         "  \"rendered_html\": string,\n"
         "  \"assets\": [{\"filename\": string, \"language\": string, \"code\": string}]\n"
         "}\n\n"
         "REPORT RULES:\n"
         "1. The primary deliverable is the report itself, not implementation material.\n"
         "2. `rendered_html` must be a complete, valid HTML document suitable for PDF conversion.\n"
         "3. The supplied template is a visual reference, not a rigid cage. You may substantially redesign layout, hierarchy, borders, cover treatment, spacing, and section presentation while preserving academic clarity and the requested theme.\n"
         "4. Only include entries in `assets` when code, setup files, or reproducible artifacts are genuinely needed for the user's request.\n"
         "5. Keep runnable code out of the report body. The PDF should read like a polished report, while implementation files live in `assets`.\n"
         "6. If charts are not useful, return an empty charts array.\n"
         "7. Never include code related to the report engine itself.\n"
         "8. Keep the report academically polished, topic-aware, and free of filler.\n"
         "9. Identity-minimal: do NOT invent or mention universities, departments, campuses, supervisors, or locations. Use only the provided author identity.\n"
         "9. Make the report rigorous and sufficiently long: target ~1500–2500 words unless the prompt is extremely narrow.\n"
         "10. Include the following sections where applicable: Introduction, Theory/Background, Methodology, Results, Discussion, Conclusion.\n"
         "11. Include at least 8 references unless the topic is exceptionally constrained.\n"
         "12. Include 1–3 charts/tables when they materially improve clarity (use table for measured data; bar/line for comparisons/trends).\n\n"
         "DECISION GUIDANCE:\n" + codeDirective + "\n\n"
         "DESIGN BRIEF:\n" + designBrief + "\n\n"
         "TEMPLATE REFERENCE:\n" + templateHtml}
    };

    json userMsg = {{"role", "user"}, {"content", prompt}};

    // Resolve provider-specific endpoint
    std::string resolvedUrl, resolvedModel;
    resolveEndpoint(resolvedUrl, resolvedModel);

    // Trim API key (crucial for Windows .env files which may have \r)
    std::string cleanKey = utils::trim(apiKey_);

    std::cout << "[Agent] LLM endpoint: " << resolvedUrl << std::endl;
    std::cout << "[Agent] LLM model: " << resolvedModel << std::endl;

    json reqBody = {
        {"model", resolvedModel},
        {"messages", json::array({systemMsg, userMsg})},
        {"temperature", 0.7},
        {"max_tokens", 4096}
    };

    // Groq often throws 403 Forbidden if response_format is requested but not supported
    // for that specific model/version. We rely on system instructions instead.
    if (resolvedUrl.find("groq.com") == std::string::npos) {
        reqBody["response_format"] = {{"type", "json_object"}};
    }

    auto resp = client.post(
        resolvedUrl,
        reqBody.dump(),
        {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + cleanKey}
        }
    );

    ReportContent content;

    if (!resp.ok()) {
        std::cerr << "[Agent] LLM request failed (HTTP " << resp.status << ")" << std::endl;
        std::cerr << "[Agent] Response body: " << resp.body.substr(0, 500) << std::endl;
        // Fall back to demo mode on API failure
        return generateDemo(prompt);
    }

    try {
        auto j = json::parse(resp.body);
        auto text = j["choices"][0]["message"]["content"].get<std::string>();
        
        // Use extractJson to handle Markdown backticks or preamble
        std::string cleanedJson = utils::extractJson(text);
        auto data = json::parse(cleanedJson);

        content.title      = data.value("title", "Untitled Report");
        content.subtitle   = data.value("subtitle", "");
        content.abstractText = data.value("abstract", "");
        content.discipline = data.value("discipline", detectDiscipline(prompt));
        content.renderedHtml = data.value("rendered_html", "");
        
        // Preserve author if already set (e.g. by server from session)
        if (content.author.empty()) {
            content.author = data.value("author", "Elpis Engine");
        }
        content.institution = data.value("institution", "Academic Institution");

        // Date
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        std::ostringstream dateStr;
        dateStr << std::put_time(&tm_buf, "%B %d, %Y");
        content.date = dateStr.str();

        // Sections
        if (data.contains("sections")) {
            for (auto& sec : data["sections"]) {
                Section s;
                s.heading = sec.value("heading", "Section");
                s.body    = sec.value("body", "");

                if (sec.contains("codeBlocks")) {
                    for (auto& cb : sec["codeBlocks"]) {
                        CodeBlock block;
                        block.filename = cb.value("filename", "script.py");
                        block.language = cb.value("language", "text");
                        block.code     = cb.value("code", "");
                        if (!block.code.empty()) s.codeBlocks.push_back(block);
                    }
                }
                if (sec.contains("charts")) {
                    for (auto& ch : sec["charts"]) {
                        ChartData cd;
                        std::string type = ch.value("type", "bar");
                        if (type == "line") cd.type = ChartData::Type::Line;
                        else if (type == "pie") cd.type = ChartData::Type::Pie;
                        else cd.type = ChartData::Type::Bar;
                        cd.title = ch.value("title", "Chart");
                        if (ch.contains("labels") && ch["labels"].is_array()) {
                            for (auto& item : ch["labels"]) {
                                if (item.is_string()) cd.labels.push_back(item.get<std::string>());
                                else if (item.is_number()) cd.labels.push_back(item.dump());
                            }
                        }
                        if (ch.contains("values") && ch["values"].is_array()) {
                            for (auto& item : ch["values"]) {
                                if (item.is_number()) cd.values.push_back(item.get<float>());
                                else if (item.is_string()) {
                                    try { cd.values.push_back(std::stof(item.get<std::string>())); } catch(...) {}
                                }
                            }
                        }
                        s.charts.push_back(cd);
                    }
                }
                content.sections.push_back(s);
            }
        }

        if (data.contains("references") && data["references"].is_array()) {
            for (auto& item : data["references"]) {
                if (item.is_string()) content.references.push_back(item.get<std::string>());
                else if (item.is_number()) content.references.push_back(item.dump());
            }
        }

        if (data.contains("assets") && data["assets"].is_array()) {
            for (auto& asset : data["assets"]) {
                CodeBlock block;
                block.filename = asset.value("filename", "asset.txt");
                block.language = asset.value("language", "text");
                block.code     = asset.value("code", "");
                if (!block.code.empty()) {
                    content.assets.push_back(block);
                }
            }
        }

        normalizeAssets(content, wantsCodeAssets);

        // Safety/quality gate: if rendered_html is present but unsafe/invalid, drop it so the server falls back to template rendering.
        if (!content.renderedHtml.empty() && !isRenderedHtmlAcceptable(content.renderedHtml)) {
            std::cerr << "[Agent] rendered_html rejected by safety gate; falling back to TemplateEngine." << std::endl;
            content.renderedHtml.clear();
        }

        // Quality gate: if too short or missing required assets, do a single "expand/repair" pass.
        const size_t minWords = wantsCodeAssets ? 1200 : 1400;
        size_t words = approximateWordCount(content.abstractText);
        for (const auto& s : content.sections) words += approximateWordCount(s.body);
        const bool missingAssets = wantsCodeAssets && content.assets.empty();

        if (words < minWords || missingAssets || content.sections.size() < 5 || content.references.size() < 8) {
            std::cout << "[Agent] Expanding/repairing report (words=" << words
                      << ", sections=" << content.sections.size()
                      << ", refs=" << content.references.size()
                      << ", missingAssets=" << (missingAssets ? "yes" : "no") << ")" << std::endl;

            json repairSystem = {
                {"role", "system"},
                {"content",
                    "Return ONLY valid JSON in the same schema as before.\n"
                    "You will be given a draft JSON report. Improve it to satisfy requirements:\n"
                    "- Expand to be rigorous and detailed (target 1500–2500 words unless truly narrow).\n"
                    "- Ensure >= 6 substantive sections when applicable.\n"
                    "- Ensure >= 8 references.\n"
                    "- If MODE=report_plus_assets: include at least 1–3 standalone runnable assets (scripts/notebooks/config) that directly support analysis.\n"
                    "- Keep runnable code in top-level assets, not inside section codeBlocks.\n"
                    "- Keep identity minimal (no universities/locations)."
                }
            };

            json repairUser = {
                {"role", "user"},
                {"content",
                    std::string("MODE=") + (wantsCodeAssets ? "report_plus_assets" : "report_only") +
                    "\nPROMPT:\n" + prompt +
                    "\n\nDESIGN_BRIEF:\n" + designBrief +
                    "\n\nTEMPLATE (for rendered_html):\n" + templateHtml +
                    "\n\nDRAFT_JSON:\n" + data.dump()
                }
            };

            json repairReq = {
                {"model", resolvedModel},
                {"messages", json::array({repairSystem, repairUser})},
                {"temperature", 0.6},
                {"max_tokens", 4096}
            };
            if (resolvedUrl.find("groq.com") == std::string::npos) {
                repairReq["response_format"] = {{"type", "json_object"}};
            }

            auto repairResp = client.post(
                resolvedUrl,
                repairReq.dump(),
                {
                    {"Content-Type", "application/json"},
                    {"Authorization", "Bearer " + cleanKey}
                }
            );

            if (repairResp.ok()) {
                try {
                    auto rj = json::parse(repairResp.body);
                    auto rtext = rj["choices"][0]["message"]["content"].get<std::string>();
                    std::string rclean = utils::extractJson(rtext);
                    auto rdata = json::parse(rclean);

                    // Re-run the same extraction logic by overwriting 'data' and re-parsing via a small local lambda.
                    // (Keep this minimal: update only the fields that commonly affect perceived quality.)
                    content.title        = rdata.value("title", content.title);
                    content.subtitle     = rdata.value("subtitle", content.subtitle);
                    content.abstractText = rdata.value("abstract", content.abstractText);
                    content.discipline   = rdata.value("discipline", content.discipline);
                    content.renderedHtml = rdata.value("rendered_html", content.renderedHtml);

                    // Sections (replace)
                    content.sections.clear();
                    if (rdata.contains("sections")) {
                        for (auto& sec : rdata["sections"]) {
                            Section s;
                            s.heading = sec.value("heading", "Section");
                            s.body    = sec.value("body", "");

                            if (sec.contains("codeBlocks")) {
                                for (auto& cb : sec["codeBlocks"]) {
                                    CodeBlock block;
                                    block.filename = cb.value("filename", "script.py");
                                    block.language = cb.value("language", "text");
                                    block.code     = cb.value("code", "");
                                    if (!block.code.empty()) s.codeBlocks.push_back(block);
                                }
                            }
                            if (sec.contains("charts")) {
                                for (auto& ch : sec["charts"]) {
                                    ChartData cd;
                                    std::string type = ch.value("type", "bar");
                                    if (type == "line") cd.type = ChartData::Type::Line;
                                    else if (type == "pie") cd.type = ChartData::Type::Pie;
                                    else cd.type = ChartData::Type::Bar;
                                    cd.title = ch.value("title", "Chart");
                                    if (ch.contains("labels") && ch["labels"].is_array()) {
                                        for (auto& item : ch["labels"]) {
                                            if (item.is_string()) cd.labels.push_back(item.get<std::string>());
                                            else if (item.is_number()) cd.labels.push_back(item.dump());
                                        }
                                    }
                                    if (ch.contains("values") && ch["values"].is_array()) {
                                        for (auto& item : ch["values"]) {
                                            if (item.is_number()) cd.values.push_back(item.get<float>());
                                            else if (item.is_string()) {
                                                try { cd.values.push_back(std::stof(item.get<std::string>())); } catch(...) {}
                                            }
                                        }
                                    }
                                    s.charts.push_back(cd);
                                }
                            }
                            content.sections.push_back(s);
                        }
                    }

                    // References (replace)
                    content.references.clear();
                    if (rdata.contains("references") && rdata["references"].is_array()) {
                        for (auto& item : rdata["references"]) {
                            if (item.is_string()) content.references.push_back(item.get<std::string>());
                            else if (item.is_number()) content.references.push_back(item.dump());
                        }
                    }

                    // Assets (replace)
                    content.assets.clear();
                    if (rdata.contains("assets") && rdata["assets"].is_array()) {
                        for (auto& asset : rdata["assets"]) {
                            CodeBlock block;
                            block.filename = asset.value("filename", "asset.txt");
                            block.language = asset.value("language", "text");
                            block.code     = asset.value("code", "");
                            if (!block.code.empty()) content.assets.push_back(block);
                        }
                    }

                    normalizeAssets(content, wantsCodeAssets);

                    if (!content.renderedHtml.empty() && !isRenderedHtmlAcceptable(content.renderedHtml)) {
                        content.renderedHtml.clear();
                    }
                } catch (...) {
                    // If repair parsing fails, keep first-pass content.
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[Agent] Error processing LLM response: " << e.what() << std::endl;
        std::cerr << "[Agent] Raw response starting with: " << resp.body.substr(0, 200) << std::endl;
        return generateDemo(prompt);
    } catch (...) {
        std::cerr << "[Agent] Unknown error processing LLM response" << std::endl;
        return generateDemo(prompt);
    }

    normalizeAssets(content, wantsCodeAssets);
    return content;
}

// Return demo content based on prompt keywords and discipline

ReportContent Agent::generateDemo(const std::string& prompt) {
    ReportContent c;

    c.discipline  = detectDiscipline(prompt);
    c.author      = "Elpis Engine";
    c.institution = "Department of " + c.discipline;

    // Current date
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &t);
    std::ostringstream dateStr;
    dateStr << std::put_time(&tm_buf, "%B %d, %Y");
    c.date = dateStr.str();

    // Generate title from prompt
    std::string promptLower = utils::toLower(prompt);
    if (prompt.size() > 10) {
        c.title = "A Comprehensive Analysis of " +
                  prompt.substr(0, std::min((size_t)80, prompt.find('.')));
    } else {
        c.title = "An Analytical Investigation into Contemporary " + c.discipline + " Methodologies";
    }
    c.subtitle = "A Formal Academic Report";

    // Abstract
    c.abstractText =
        "This report presents a rigorous investigation into the subject matter specified by the "
        "research prompt. Through systematic analysis and critical evaluation of existing literature, "
        "this study seeks to contribute meaningful insights to the broader academic discourse within "
        "the field of " + c.discipline + ". The methodology employed herein adheres to established "
        "research paradigms, ensuring both validity and reproducibility. Preliminary findings suggest "
        "significant implications for both theoretical understanding and practical application. "
        "The analysis encompasses quantitative evaluation, comparative assessment, and synthesis of "
        "contemporary research methodologies. Results are presented with appropriate statistical "
        "measures and visualised through comprehensive data representations. The conclusions drawn "
        "from this investigation offer a robust foundation for subsequent scholarly inquiry and "
        "contribute to the evolving body of knowledge within the discipline.";

    // Section 1: Introduction
    {
        Section s;
        s.heading = "1. Introduction";
        s.body =
            "The field of " + c.discipline + " has witnessed unprecedented growth and transformation "
            "in recent decades, driven by technological advancements, evolving theoretical frameworks, "
            "and an increasing demand for evidence-based approaches to complex problems. This report "
            "addresses the core research question posed in the original prompt, situating the "
            "investigation within the broader context of contemporary academic inquiry.\n\n"
            "The significance of this research lies in its potential to bridge existing gaps in the "
            "literature whilst providing practical insights applicable to real-world scenarios. "
            "As noted by numerous scholars in the field, the intersection of theoretical rigour "
            "and practical applicability remains a persistent challenge, one that this report "
            "endeavours to address through a multi-faceted analytical approach.\n\n"
            "The remainder of this document is organised as follows: Section 2 presents the "
            "theoretical background and literature review; Section 3 details the methodology "
            "employed; Section 4 presents the results and analysis; Section 5 offers a discussion "
            "of the findings; and Section 6 concludes with recommendations for future work.";
        c.sections.push_back(s);
    }

    // Section 2: Literature Review
    {
        Section s;
        s.heading = "2. Literature Review and Theoretical Background";
        s.body =
            "A comprehensive review of the existing body of literature reveals several key themes "
            "and research trajectories pertinent to the present investigation. The theoretical "
            "foundations upon which this work is built draw from seminal contributions by established "
            "scholars, whilst also incorporating more recent developments that have reshaped the "
            "disciplinary landscape.\n\n"
            "The evolution of analytical methodologies within " + c.discipline + " has been marked "
            "by a progressive shift from purely qualitative approaches toward increasingly "
            "quantitative and computational frameworks. This paradigmatic transition has enabled "
            "researchers to address questions of greater complexity and nuance, though not without "
            "attendant epistemological challenges.\n\n"
            "Several theoretical models have been proposed to account for the phenomena under "
            "investigation. The most influential among these include parametric analysis frameworks, "
            "comparative evaluation matrices, and multi-variable regression models. Each offers "
            "distinct advantages and limitations, which are considered in the selection of the "
            "methodology for this study.\n\n"
            "Furthermore, the interdisciplinary nature of contemporary research necessitates "
            "engagement with adjacent fields, including statistical theory, computational science, "
            "and domain-specific knowledge bases. This cross-pollination of ideas has proven "
            "particularly fruitful in generating novel insights and methodological innovations.";
        c.sections.push_back(s);
    }

    // Section 3: Methodology
    {
        Section s;
        s.heading = "3. Methodology";
        s.body =
            "The methodological framework adopted for this investigation follows established "
            "research protocols within the discipline, ensuring both rigour and reproducibility. "
            "The approach consists of three primary phases: data collection and preprocessing, "
            "analytical processing, and validation through comparative assessment.\n\n"
            "Data were sourced from authoritative repositories and subjected to quality assurance "
            "procedures to mitigate the influence of anomalous or erroneous entries. The preprocessing "
            "pipeline included normalisation, outlier detection, and feature extraction, each "
            "implemented according to best practices documented in the relevant literature.\n\n"
            "The analytical phase employed a combination of quantitative methods, selected for their "
            "demonstrated efficacy in addressing research questions of the type posed herein. "
            "Statistical significance was assessed at the alpha = 0.05 level, with corrections "
            "applied for multiple comparisons where appropriate.\n\n"
            "Validation was conducted through k-fold cross-validation (k = 10) and independent "
            "holdout testing, providing robust estimates of model performance and generalisability. "
            "All computational procedures were implemented in a reproducible manner, with source "
            "code provided in the appendices.";

        // Add sample code if discipline is technical
        if (topicRequiresCode(prompt)) {
            CodeBlock cb;
            cb.language = "python";
            cb.filename = "analysis_utils.py";
            cb.code =
                "import numpy as np\n"
                "from sklearn.model_selection import cross_val_score\n"
                "from sklearn.preprocessing import StandardScaler\n\n"
                "def evaluate_model(model, X_train, y_train):\n"
                "    scaler = StandardScaler()\n"
                "    X_scaled = scaler.fit_transform(X_train)\n"
                "    scores = cross_val_score(model, X_scaled, y_train, cv=10, scoring='accuracy')\n"
                "    return {\n"
                "        'mean_accuracy': float(scores.mean()),\n"
                "        'std_accuracy': float(scores.std())\n"
                "    }\n";
            c.assets.push_back(cb);
        }
        c.sections.push_back(s);
    }

    // Section 4: Results
    {
        Section s;
        s.heading = "4. Results and Analysis";
        s.body =
            "The results obtained from the analytical procedures described above are presented "
            "in this section, accompanied by appropriate statistical measures and visual "
            "representations. The data reveal several noteworthy patterns that merit detailed "
            "examination and interpretation.\n\n"
            "The primary metric of evaluation yielded a mean performance score of 0.934 "
            "(SD = 0.021), indicating robust and consistent results across experimental "
            "conditions. Comparative analysis with baseline methods demonstrates a statistically "
            "significant improvement (p < 0.001), confirming the efficacy of the proposed approach.\n\n"
            "Table 1 presents the quantitative results across all experimental conditions, "
            "whilst the accompanying figure provides a visual summary of the performance "
            "distribution. The results consistently exceed established benchmarks reported "
            "in the literature, suggesting that the methodology offers genuine advantages "
            "over existing alternatives.\n\n"
            "A detailed breakdown by category reveals differential performance across subgroups, "
            "with the most pronounced improvements observed in conditions of high complexity. "
            "This finding aligns with the theoretical predictions outlined in Section 2 and "
            "supports the hypothesis that the proposed framework is particularly well-suited "
            "to challenging problem instances.";

        // Bar chart
        ChartData barChart;
        barChart.type = ChartData::Type::Bar;
        barChart.title = "Performance Comparison Across Methods";
        barChart.labels = {"Baseline", "Method A", "Method B", "Proposed", "Upper Bound"};
        barChart.values = {72.5f, 81.3f, 85.7f, 93.4f, 98.0f};
        s.charts.push_back(barChart);

        // Table
        ChartData table;
        table.type = ChartData::Type::Table;
        table.title = "Table 1: Quantitative Results Summary";
        table.headers = {"Metric", "Baseline", "Proposed", "Improvement"};
        table.rows = {
            {"Accuracy",  "0.725", "0.934", "+28.8%"},
            {"Precision", "0.710", "0.921", "+29.7%"},
            {"Recall",    "0.698", "0.947", "+35.7%"},
            {"F1-Score",  "0.704", "0.934", "+32.7%"},
            {"AUC-ROC",   "0.812", "0.968", "+19.2%"},
        };
        s.charts.push_back(table);

        c.sections.push_back(s);
    }

    // Section 5: Discussion
    {
        Section s;
        s.heading = "5. Discussion";
        s.body =
            "The findings presented in the preceding section warrant careful interpretation "
            "within the broader context of the research landscape. The observed improvements "
            "in performance metrics, while substantial, must be considered alongside the "
            "methodological choices and potential limitations inherent in the study design.\n\n"
            "The superior performance of the proposed approach can be attributed to several "
            "factors. Firstly, the preprocessing pipeline effectively addresses data quality "
            "issues that have historically impaired the performance of competing methods. "
            "Secondly, the analytical framework is capable of capturing non-linear relationships "
            "and complex interactions within the data, which simpler models may fail to detect.\n\n"
            "It is important to acknowledge certain limitations of the present study. The "
            "generalisability of the findings may be constrained by the specific characteristics "
            "of the datasets employed. Additionally, the computational complexity of the proposed "
            "method, while manageable for the problem sizes considered here, may present scalability "
            "challenges in large-scale applications.\n\n"
            "Nevertheless, the results provide compelling evidence in support of the proposed "
            "methodology and suggest several promising avenues for future investigation. The "
            "integration of additional data sources, the exploration of ensemble techniques, "
            "and the application to related problem domains each represent viable directions "
            "for extending the present work.";

        // Line chart showing trends
        ChartData lineChart;
        lineChart.type = ChartData::Type::Line;
        lineChart.title = "Performance Trend Over Iterations";
        lineChart.labels = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
        lineChart.values = {68.2f, 74.1f, 79.8f, 83.5f, 86.9f, 89.4f, 91.2f, 92.4f, 93.1f, 93.4f};
        s.charts.push_back(lineChart);

        c.sections.push_back(s);
    }

    // Section 6: Conclusion
    {
        Section s;
        s.heading = "6. Conclusion and Future Work";
        s.body =
            "This report has presented a comprehensive investigation into the subject matter "
            "specified in the research prompt, employing rigorous analytical methods and "
            "adhering to established academic standards throughout. The principal findings "
            "demonstrate the viability and efficacy of the proposed approach, with quantitative "
            "results exceeding established benchmarks in the field.\n\n"
            "The contributions of this work are threefold: first, we have provided a systematic "
            "review and synthesis of the existing literature; second, we have proposed and "
            "validated an improved analytical methodology; and third, we have identified "
            "specific areas where further research is likely to yield productive results.\n\n"
            "Future work should focus on extending the present methodology to larger and more "
            "diverse datasets, exploring the integration of complementary analytical techniques, "
            "and conducting longitudinal studies to assess the stability of the observed "
            "improvements over time. Additionally, the development of optimised implementations "
            "suitable for resource-constrained environments represents a worthwhile engineering "
            "objective.\n\n"
            "In conclusion, the evidence accumulated through this investigation provides a "
            "robust foundation for continued scholarly inquiry and offers practical insights "
            "that may inform decision-making processes within the field of " + c.discipline + ".";
        c.sections.push_back(s);
    }

    // References
    c.references = {
        "[1] Smith, J. A., & Johnson, M. R. (2023). Advances in Analytical Methodologies: A Comprehensive Survey. Journal of " + c.discipline + " Research, 45(3), 112-134.",
        "[2] Williams, K. L. (2022). Computational Approaches to Complex Problem Solving. Academic Press, London.",
        "[3] Chen, Y., & Patel, R. (2024). Performance Evaluation Frameworks in Contemporary Research. International Conference on " + c.discipline + ", pp. 445-458.",
        "[4] Thompson, A. B., Garcia, M., & Lee, S. (2023). Statistical Methods for Robust Analysis. Springer Series in Applied Sciences, Vol. 12.",
        "[5] Anderson, P. Q. (2021). Foundations of Modern " + c.discipline + ": Theory and Practice. Cambridge University Press.",
        "[6] Brown, E. R., & Davis, L. M. (2024). Emerging Trends and Future Directions: A Critical Review. Annual Review of " + c.discipline + ", 18, 67-89.",
        "[7] Martinez, C. S. (2023). Data-Driven Decision Making in Academic Research. Oxford Academic Publishing.",
        "[8] Taylor, R. J., & Wilson, H. (2022). Reproducibility and Validity in Quantitative Research Methods. Journal of Research Methodology, 31(2), 201-219.",
    };

    return c;
}

// End of Agent implementation
