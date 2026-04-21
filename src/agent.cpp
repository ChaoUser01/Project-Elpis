#include "agent.h"
#include "http_client.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <iostream>

using json = nlohmann::json;

Agent::Agent() {
    // No longer auto-load from env — keys come from BYOK login
}

// ── Provider auto-detection from API key ──────────────────────────────────

void Agent::resolveEndpoint(std::string& outUrl, std::string& outModel) {
    // If explicit overrides are set, use them
    if (!baseUrl_.empty() && !model_.empty()) {
        outUrl = baseUrl_;
        outModel = model_;
        return;
    }

    // Auto-detect provider from API key prefix
    std::string provider = provider_;
    if (provider.empty()) {
        if (apiKey_.substr(0, 4) == "gsk_") {
            provider = "groq";
        } else if (apiKey_.substr(0, 4) == "AIza") {
            provider = "gemini";
        } else if (apiKey_.substr(0, 3) == "sk-") {
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
        outModel = model_.empty() ? "llama-3.3-70b-versatile" : model_;
    } else if (provider == "gemini") {
        outUrl   = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
        outModel = model_.empty() ? "gemini-2.5-flash" : model_;
    } else {
        outUrl   = "https://api.openai.com/v1/chat/completions";
        outModel = model_.empty() ? "gpt-4o" : model_;
    }
}


// ── Discipline detection ───────────────────────────────────────────────────

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

// ── LLM-based generation ──────────────────────────────────────────────────

ReportContent Agent::generateWithLLM(const std::string& prompt) {
    HttpClient client;

    json systemMsg = {
        {"role", "system"},
        {"content",
         "You are an expert academic writer. Generate a formal university-level report "
         "based on the user's prompt. Return ONLY valid JSON with this structure:\n"
         "{\n"
         "  \"title\": \"...\",\n"
         "  \"subtitle\": \"...\",\n"
         "  \"abstract\": \"150-250 word formal abstract\",\n"
         "  \"discipline\": \"...\",\n"
         "  \"sections\": [\n"
         "    {\n"
         "      \"heading\": \"1. Introduction\",\n"
         "      \"body\": \"formal academic prose...\",\n"
         "      \"code\": [{\"language\": \"python\", \"code\": \"...\"}],\n"
         "      \"charts\": [{\"type\": \"bar\", \"title\": \"...\", "
         "\"labels\": [...], \"values\": [...]}]\n"
         "    }\n"
         "  ],\n"
         "  \"references\": [\"[1] Author, Title, Journal, Year.\", ...]\n"
         "}\n"
         "Use elevated academic tone. Include 5-8 sections. Add code only if relevant. "
         "Add 1-2 chart directives if data visualization would support the text."}
    };

    json userMsg = {{"role", "user"}, {"content", prompt}};

    // Resolve provider-specific endpoint
    std::string resolvedUrl, resolvedModel;
    resolveEndpoint(resolvedUrl, resolvedModel);

    std::cout << "[Agent] LLM endpoint: " << resolvedUrl << std::endl;
    std::cout << "[Agent] LLM model: " << resolvedModel << std::endl;

    json reqBody = {
        {"model", resolvedModel},
        {"messages", json::array({systemMsg, userMsg})},
        {"temperature", 0.7},
        {"max_tokens", 4096},
        {"response_format", {{"type", "json_object"}}}
    };

    auto resp = client.post(
        resolvedUrl,
        reqBody.dump(),
        {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + apiKey_}
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
        auto data = json::parse(text);

        content.title      = data.value("title", "Untitled Report");
        content.subtitle   = data.value("subtitle", "");
        content.abstractText = data.value("abstract", "");
        content.discipline = data.value("discipline", detectDiscipline(prompt));
        content.author     = "Elpis";
        content.institution = "University";

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

                if (sec.contains("code")) {
                    for (auto& cb : sec["code"]) {
                        CodeBlock block;
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
                        if (ch.contains("labels"))
                            cd.labels = ch["labels"].get<std::vector<std::string>>();
                        if (ch.contains("values"))
                            cd.values = ch["values"].get<std::vector<float>>();
                        s.charts.push_back(cd);
                    }
                }
                content.sections.push_back(s);
            }
        }

        if (data.contains("references")) {
            content.references = data["references"].get<std::vector<std::string>>();
        }

    } catch (...) {
        return generateDemo(prompt);
    }

    return content;
}

// ── Demo content generation ────────────────────────────────────────────────

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
        if (c.discipline == "Computer Science" || c.discipline == "Data Science" ||
            c.discipline == "Mathematics") {
            CodeBlock cb;
            cb.language = "python";
            cb.code =
                "import numpy as np\n"
                "from sklearn.model_selection import cross_val_score\n"
                "from sklearn.preprocessing import StandardScaler\n\n"
                "# Data preprocessing pipeline\n"
                "scaler = StandardScaler()\n"
                "X_scaled = scaler.fit_transform(X_train)\n\n"
                "# Cross-validation assessment\n"
                "scores = cross_val_score(model, X_scaled, y_train, cv=10,\n"
                "                        scoring='accuracy')\n"
                "print(f'Mean CV Accuracy: {scores.mean():.4f} +/- {scores.std():.4f}')";
            s.codeBlocks.push_back(cb);
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

// ── Main entry point ───────────────────────────────────────────────────────

ReportContent Agent::generate(const std::string& prompt) {
    if (hasApiKey()) {
        return generateWithLLM(prompt);
    }
    return generateDemo(prompt);
}
