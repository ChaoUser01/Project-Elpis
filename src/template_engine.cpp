#include "template_engine.h"
#include "utils.h"
#include <sstream>
#include <regex>
#include <iostream>
#include <vector>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

// Split string by delimiter
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

// Resolve a JSON value by dot-notation path (e.g. "section.codeBlocks")
static const json* resolvePath(const json& ctx, const std::string& path) {
    auto parts = split(path, '.');
    const json* current = &ctx;
    for (const auto& part : parts) {
        if (current->is_object() && current->contains(part)) {
            current = &((*current)[part]);
        } else {
            return nullptr;
        }
    }
    return current;
}

// Find matching end tag (e.g. {% endfor %}) while accounting for nesting
static size_t findMatchingTag(const std::string& s, size_t startPos, const std::string& openTagPattern, const std::string& closeTag) {
    int level = 1;
    size_t pos = startPos;
    while (level > 0 && pos < s.size()) {
        size_t nextOpen = s.find(openTagPattern, pos);
        size_t nextClose = s.find(closeTag, pos);

        if (nextClose == std::string::npos) return std::string::npos;

        if (nextOpen != std::string::npos && nextOpen < nextClose) {
            level++;
            pos = nextOpen + openTagPattern.size();
        } else {
            level--;
            if (level == 0) return nextClose;
            pos = nextClose + closeTag.size();
        }
    }
    return std::string::npos;
}

static std::string renderRecursive(const std::string& tmpl, const json& ctx) {
    std::string result = tmpl;
    size_t pos = 0;

    while (pos < result.size()) {
        size_t tagStart = result.find("{%", pos);
        size_t varStart = result.find("{{", pos);

        // Control Flow {% ... %}
        if (tagStart != std::string::npos && (varStart == std::string::npos || tagStart < varStart)) {
            size_t tagEnd = result.find("%}", tagStart);
            if (tagEnd == std::string::npos) { pos = tagStart + 2; continue; }

            std::string tagContent = trim(result.substr(tagStart + 2, tagEnd - tagStart - 2));

            // FOR LOOP: {% for ITEM in LIST %}
            if (tagContent.substr(0, 4) == "for ") {
                std::istringstream iss(tagContent.substr(4));
                std::string itemKey, inWord, arrayPath;
                iss >> itemKey >> inWord >> arrayPath;

                size_t endTagPos = findMatchingTag(result, tagEnd + 2, "{% for ", "{% endfor %}");
                if (endTagPos == std::string::npos) { pos = tagEnd + 2; continue; }

                std::string body = result.substr(tagEnd + 2, endTagPos - (tagEnd + 2));
                std::string expanded;

                const json* arrayVal = resolvePath(ctx, arrayPath);
                if (arrayVal && arrayVal->is_array()) {
                    for (const auto& item : *arrayVal) {
                        json subCtx = ctx;
                        subCtx[itemKey] = item;
                        expanded += renderRecursive(body, subCtx);
                    }
                }

                result.replace(tagStart, (endTagPos + 12) - tagStart, expanded);
                pos = tagStart + expanded.size();
                continue;
            }
            // IF STATEMENT: {% if VAR %}
            else if (tagContent.substr(0, 3) == "if ") {
                std::string conditionPath = trim(tagContent.substr(3));
                size_t endTagPos = findMatchingTag(result, tagEnd + 2, "{% if ", "{% endif %}");
                if (endTagPos == std::string::npos) { pos = tagEnd + 2; continue; }

                std::string body = result.substr(tagEnd + 2, endTagPos - (tagEnd + 2));
                
                const json* val = resolvePath(ctx, conditionPath);
                bool truthy = false;
                if (val) {
                    if (val->is_boolean()) truthy = val->get<bool>();
                    else if (val->is_number()) truthy = val->get<double>() != 0;
                    else if (val->is_string()) truthy = !val->get<std::string>().empty();
                    else if (val->is_array() || val->is_object()) truthy = !val->empty();
                }

                std::string renderedBody = truthy ? renderRecursive(body, ctx) : "";
                result.replace(tagStart, (endTagPos + 11) - tagStart, renderedBody);
                pos = tagStart + renderedBody.size();
                continue;
            }
            pos = tagEnd + 2;
        }
        // Variable {{ ... }}
        else if (varStart != std::string::npos) {
            size_t varEnd = result.find("}}", varStart);
            if (varEnd == std::string::npos) { pos = varStart + 2; continue; }

            std::string varPath = trim(result.substr(varStart + 2, varEnd - varStart - 2));
            std::string valueStr;

            const json* val = resolvePath(ctx, varPath);
            if (val) {
                if (val->is_string()) valueStr = utils::escapeHtml(val->get<std::string>());
                else valueStr = utils::escapeHtml(val->dump());
            }

            result.replace(varStart, (varEnd + 2) - varStart, valueStr);
            pos = varStart + valueStr.size();
        }
        else {
            break;
        }
    }
    return result;
}

std::string TemplateEngine::render(const std::string& tmpl, const nlohmann::json& data) {
    try {
        return renderRecursive(tmpl, data);
    } catch (const std::exception& e) {
        std::cerr << "[TemplateEngine] Critical Error: " << e.what() << std::endl;
        return tmpl;
    }
}
