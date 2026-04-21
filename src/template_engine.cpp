#include "template_engine.h"
#include <sstream>
#include <regex>

// ── Template rendering ─────────────────────────────────────────────────────

std::string TemplateEngine::render(const std::string& tmpl,
                                    const VarMap& vars,
                                    const ListMap& lists) {
    std::string result = tmpl;

    // Pass 1: Process {% for item in listName %}...{% endfor %}
    // We do a manual scan since nested templates could get complex with regex
    size_t searchPos = 0;
    while (true) {
        size_t forStart = result.find("{%", searchPos);
        if (forStart == std::string::npos) break;

        size_t forEnd = result.find("%}", forStart);
        if (forEnd == std::string::npos) break;

        std::string tag = result.substr(forStart + 2, forEnd - forStart - 2);

        // Trim
        auto trim = [](const std::string& s) {
            auto b = s.find_first_not_of(" \t");
            auto e = s.find_last_not_of(" \t");
            return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
        };
        tag = trim(tag);

        // Check if it's a "for" tag
        if (tag.substr(0, 4) == "for ") {
            // Parse: "for ITEM in LIST"
            std::istringstream iss(tag.substr(4));
            std::string itemName, inWord, listName;
            iss >> itemName >> inWord >> listName;

            if (inWord != "in") { searchPos = forEnd + 2; continue; }

            // Find matching endfor
            std::string endTag = "{% endfor %}";
            size_t bodyStart = forEnd + 2;
            size_t endPos = result.find(endTag, bodyStart);
            if (endPos == std::string::npos) {
                // Try without spaces
                endTag = "{%endfor%}";
                endPos = result.find(endTag, bodyStart);
            }
            if (endPos == std::string::npos) { searchPos = forEnd + 2; continue; }

            std::string body = result.substr(bodyStart, endPos - bodyStart);

            // Expand loop
            std::string expanded;
            auto lit = lists.find(listName);
            if (lit != lists.end()) {
                for (auto& itemVars : lit->second) {
                    std::string iteration = body;
                    // Replace {{ item.key }} patterns
                    for (auto& [k, v] : itemVars) {
                        std::string pattern1 = "{{ " + itemName + "." + k + " }}";
                        std::string pattern2 = "{{" + itemName + "." + k + "}}";
                        size_t p = 0;
                        while ((p = iteration.find(pattern1, p)) != std::string::npos)
                            iteration.replace(p, pattern1.size(), v);
                        p = 0;
                        while ((p = iteration.find(pattern2, p)) != std::string::npos)
                            iteration.replace(p, pattern2.size(), v);
                    }
                    expanded += iteration;
                }
            }

            result = result.substr(0, forStart) + expanded +
                     result.substr(endPos + endTag.size());
            // Don't advance searchPos — re-scan from the same point
            continue;
        }
        // Check if it's an "if" tag
        else if (tag.substr(0, 3) == "if ") {
            std::string varName = trim(tag.substr(3));

            std::string endTag = "{% endif %}";
            size_t bodyStart = forEnd + 2;
            size_t endPos = result.find(endTag, bodyStart);
            if (endPos == std::string::npos) {
                endTag = "{%endif%}";
                endPos = result.find(endTag, bodyStart);
            }
            if (endPos == std::string::npos) { searchPos = forEnd + 2; continue; }

            std::string body = result.substr(bodyStart, endPos - bodyStart);

            // Check if variable is truthy
            auto vit = vars.find(varName);
            bool truthy = (vit != vars.end() && !vit->second.empty() && vit->second != "0");

            result = result.substr(0, forStart) +
                     (truthy ? body : "") +
                     result.substr(endPos + endTag.size());
            continue;
        }

        searchPos = forEnd + 2;
    }

    // Pass 2: Replace {{ variable }} placeholders
    for (auto& [key, val] : vars) {
        std::string pattern1 = "{{ " + key + " }}";
        std::string pattern2 = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(pattern1, pos)) != std::string::npos)
            result.replace(pos, pattern1.size(), val);
        pos = 0;
        while ((pos = result.find(pattern2, pos)) != std::string::npos)
            result.replace(pos, pattern2.size(), val);
    }

    return result;
}
