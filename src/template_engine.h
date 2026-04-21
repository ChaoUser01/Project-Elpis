#pragma once
#include <string>
#include <map>
#include <vector>

// ── Simple template engine (Jinja2-like syntax) ────────────────────────────
// Supports:  {{ variable }}
//            {% for item in list %}...{% endfor %}
//            {% if variable %}...{% endif %}

class TemplateEngine {
public:
    using VarMap  = std::map<std::string, std::string>;
    using ListMap = std::map<std::string, std::vector<VarMap>>;

    // Render a template string with the given variables and lists.
    static std::string render(const std::string& tmpl,
                              const VarMap& vars,
                              const ListMap& lists = {});
};
