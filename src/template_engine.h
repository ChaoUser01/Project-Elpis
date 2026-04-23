#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Simple JSON-driven template engine (Jinja2-like syntax)
// Supports:  {{ variable }}
//            {% for item in array %}...{% endfor %}
//            {% if variable %}...{% endif %}
//            Nested tags and recursive expansion.

class TemplateEngine {
public:
    // Render a template string with the given JSON context.
    static std::string render(const std::string& tmpl, const nlohmann::json& data);
};
