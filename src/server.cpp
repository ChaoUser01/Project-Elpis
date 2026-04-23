#include "server.h"
#include "agent.h"
#include "ocr_engine.h"
#include "style_profile.h"
#include "pdf_engine.h"
#include "utils.h"
#include "template_engine.h"
#include "html_converter.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

Server::Server() {
    baseDir_ = utils::getExecutableDir();
    ledger_.setDataDir(baseDir_);
}

void Server::start() {
    httplib::Server svr;

    std::string staticDir  = baseDir_ + "/static";
    std::string outputDir  = baseDir_ + "/outputs";
    utils::ensureDir(outputDir);
    const fs::path outputRoot = fs::weakly_canonical(fs::path(outputDir));

    auto getSessionFromRequest = [&](const httplib::Request& req) -> std::optional<AuthSession> {
        std::string auth = req.get_header_value("Authorization");
        if (auth.size() <= 7 || auth.substr(0, 7) != "Bearer ") {
            return std::nullopt;
        }
        return ledger_.validateSession(auth.substr(7));
    };

    auto canAccessSession = [&](const httplib::Request& req, const SessionState& state) -> bool {
        if (state.ownerStudentId.empty()) {
            return true;
        }
        auto authSession = getSessionFromRequest(req);
        return authSession.has_value() && authSession->studentId == state.ownerStudentId;
    };

    auto getDesignBrief = [&](const std::string& studentId) -> std::string {
        if (studentId == "202453460047") {
            return "Theme: black-and-white editorial research dossier. Use monochrome contrast, precise rules, restrained texture, serif-forward typography, and a disciplined grayscale palette. Make it feel premium, print-first, and sharply structured without introducing color accents.";
        }
        if (studentId == "202453460009") {
            return "Theme: deep navy academic folio. Elegant cover treatment, strong section dividers, balanced whitespace, and a formal institutional tone.";
        }
        if (studentId == "202453460034") {
            return "Theme: warm archival paper style. Sepia-leaning neutrals, subtle panel framing, classic scholarship feel, and visually rich but still print-ready composition.";
        }
        if (studentId == "202453460052") {
            return "Theme: crisp contemporary laboratory brief. Clean geometry, restrained green accents, modern spacing rhythm, and high legibility.";
        }
        if (studentId == "202453460073") {
            return "Theme: slate and indigo analytical report. Strong hierarchy, understated grid logic, and polished comparison/table presentation.";
        }
        if (studentId == "202453460017") {
            return "Theme: refined bronze-and-charcoal thesis layout. Formal cover composition, confident headings, and a classic long-form reading experience.";
        }
        return "Theme: professional academic report. You may expand and redesign the provided template substantially, but keep it single-file, print-ready, and suitable for PDF conversion.";
    };

    // Serve static files
    svr.set_mount_point("/static", staticDir);

    // Root → index.html
    svr.Get("/", [&](const httplib::Request& /*req*/, httplib::Response& res) {
        std::string indexPath = staticDir + "/index.html";
        std::string content = utils::readFile(indexPath);
        if (content.empty()) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
            return;
        }
        res.set_content(content, "text/html");
    });

    // Health check
    svr.Get("/api/health", [&](const httplib::Request& /*req*/, httplib::Response& res) {
        json j = {
            {"status", "ok"},
            {"engine", "Elpis C++ Engine"},
            {"version", "2.0.0"}
        };
        res.set_content(j.dump(), "application/json");
    });

    // Get student list
    svr.Get("/api/students", [&](const httplib::Request& /*req*/, httplib::Response& res) {
        auto list = ledger_.getStudentList();
        json arr = json::array();
        for (auto& [id, name] : list) {
            auto user = ledger_.getUser(id);
            arr.push_back({
                {"id", id},
                {"name", name},
                {"claimed", user.has_value() && !user->passwordHash.empty()},
                {"hasSavedKey", ledger_.hasSavedKey(id)}
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    // Claim account
    svr.Post("/api/claim", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid JSON"}}).dump(), "application/json");
            return;
        }

        std::string studentId = body.value("student_id", "");
        std::string passphrase = body.value("passphrase", "");

        std::string err = ledger_.claim(studentId, passphrase);
        if (!err.empty()) {
            res.status = 400;
            res.set_content(json({{"error", err}}).dump(), "application/json");
            return;
        }

        res.set_content(json({{"success", true}, {"message", "Account claimed successfully. You may now log in."}}).dump(), "application/json");
    });

    // Login with BYOK
    svr.Post("/api/login", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid JSON"}}).dump(), "application/json");
            return;
        }

        std::string studentId  = body.value("student_id", "");
        std::string passphrase = body.value("passphrase", "");
        std::string apiKey     = body.value("api_key", "");

        std::string outError;
        std::string token = ledger_.login(studentId, passphrase, apiKey, outError);

        if (token.empty()) {
            res.status = 401;
            res.set_content(json({{"error", outError}}).dump(), "application/json");
            return;
        }

        auto user = ledger_.getUser(studentId);
        res.set_content(json({
            {"token", token},
            {"name", user.has_value() ? user->name : ""},
            {"student_id", studentId}
        }).dump(), "application/json");
    });

    // Logout
    svr.Post("/api/logout", [&](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.substr(0, 7) == "Bearer ") {
            ledger_.logout(auth.substr(7));
        }
        res.set_content(json({{"success", true}}).dump(), "application/json");
    });

    // Current user info
    svr.Get("/api/me", [&](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.size() <= 7 || auth.substr(0, 7) != "Bearer ") {
            res.status = 401;
            res.set_content(json({{"error", "Not authenticated"}}).dump(), "application/json");
            return;
        }

        auto session = ledger_.validateSession(auth.substr(7));
        if (!session.has_value()) {
            res.status = 401;
            res.set_content(json({{"error", "Session expired or invalid"}}).dump(), "application/json");
            return;
        }

        res.set_content(json({
            {"name", session->name},
            {"student_id", session->studentId},
            {"hasApiKey", !session->apiKey.empty()}
        }).dump(), "application/json");
    });

    // Generate report
    svr.Post("/generate", [&](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::steady_clock::now();

        std::cout << "[Elpis] Received generation request (async)" << std::endl;

        // ── Extract auth token (optional: allows demo mode if no auth)
        std::string authToken;
        std::string userApiKey;
        std::string userProvider;
        std::string styleIdentity;  // what seeds the style profile
        std::string userName;

        std::string auth = req.get_header_value("Authorization");
        if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") {
            authToken = auth.substr(7);
            auto session = ledger_.validateSession(authToken);
            if (session.has_value()) {
                userApiKey    = session->apiKey;
                userProvider  = session->provider;
                styleIdentity = session->studentId;  // Deterministic per student
                userName      = session->name;
                std::cout << "[Elpis] Authenticated: " << userName
                          << " | Provider: " << userProvider << std::endl;
            }
        }

        // Extract prompt from multipart form or JSON body
        std::string prompt;
        std::vector<uint8_t> imageBytes;
        std::string imageFilename;

        if (req.has_file("prompt")) {
            prompt = req.get_file_value("prompt").content;
        }
        if (req.has_param("prompt")) {
            prompt = req.get_param_value("prompt");
        }
        if (req.has_file("file")) {
            auto file = req.get_file_value("file");
            imageBytes.assign(file.content.begin(), file.content.end());
            imageFilename = file.filename;
        }

        // If no prompt and no image
        if (prompt.empty() && imageBytes.empty()) {
            try {
                auto j = json::parse(req.body);
                prompt = j.value("prompt", "");
            } catch (...) {}
        }

        if (prompt.empty() && imageBytes.empty()) {
            res.status = 400;
            json err = {{"error", "No prompt or image provided"}};
            res.set_content(err.dump(), "application/json");
            return;
        }

        // ── Generate session ID
        std::string sessionId = utils::generateSessionId();

        // Determine the API key to use (priority: user BYOK > global .env > demo)
        std::string effectiveApiKey = userApiKey.empty() ? apiKey_ : userApiKey;

        // Determine style seed (priority: student ID > random session)
        std::string styleSeed = styleIdentity.empty() ? sessionId : styleIdentity;
        std::string designBrief = getDesignBrief(styleSeed);
        std::string templatePath = ledger_.resolveTemplatePath(styleSeed);
        std::string tmplContent = utils::readFile(templatePath);
        if (tmplContent.empty()) {
            tmplContent = utils::readFile(baseDir_ + "/templates/profiles/default_academic.html");
        }

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_[sessionId] = SessionState();
            sessions_[sessionId].ownerStudentId = styleIdentity;
        }

        // ── Spawn background thread
        std::thread([this, prompt, imageBytes, imageFilename, sessionId,
                     outputDir, effectiveApiKey, userProvider, styleSeed, styleIdentity, userName, tmplContent, designBrief]() mutable {
            auto start = std::chrono::steady_clock::now();
            auto updateStatus = [&](const std::string& status) {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                sessions_[sessionId].status = status;
                std::cout << "[Session " << sessionId.substr(0, 6) << "] " << status << std::endl;
            };

            updateStatus("Initializing pipeline...");

            // ── Step 1: OCR if image provided
            if (!imageBytes.empty()) {
                updateStatus("Extracting text from image via OCR...");
                OcrEngine ocr;
                if (ocr.isAvailable()) {
                    std::string extracted = ocr.extractText(imageBytes);
                    if (!extracted.empty() && extracted[0] != '[') {
                        prompt += "\n\n[Extracted from image]:\n" + extracted;
                    }
                }
            }

            if (prompt.empty()) {
                prompt = "Generate a comprehensive academic report on contemporary research methodologies.";
            }

            // ── Step 2: Generate style profile (seeded by student ID if authenticated)
            updateStatus("Designing academic style profile...");
            StyleProfile style = generateStyleProfile(styleSeed);

            // ── Step 3: Run agent with user's BYOK key and provider
            updateStatus("Drafting report content via LLM...");
            Agent agent;
            if (!effectiveApiKey.empty()) {
                agent.setApiKey(effectiveApiKey);
                if (!userProvider.empty()) {
                    agent.setProvider(userProvider);
                }
            }
            ReportContent content = agent.generate(prompt, tmplContent, designBrief);

            // Override author with authenticated user name if available
            if (!userName.empty()) {
                content.author = userName;
            }

            // ── Step 4: Render HTML via template
            updateStatus("Rendering Academic HTML template...");
            
            // Create JSON context for the template (Directly from content)
            json templateData;
            templateData["title"]       = content.title;
            templateData["subtitle"]    = content.subtitle;
            templateData["author"]      = content.author;
            // Identity-minimal header: only name + student ID (no institution branding).
            templateData["institution"] = "";
            templateData["student_id"]  = styleIdentity;
            templateData["date"]        = content.date;
            templateData["discipline"]  = content.discipline;
            templateData["abstract"]    = content.abstractText;

            // Map sections with nested code blocks and charts
            templateData["sections"] = json::array();
            for (const auto& s : content.sections) {
                json sJson;
                sJson["heading"] = s.heading;
                sJson["body"]    = s.body;

                // Code Blocks
                sJson["codeBlocks"] = json::array();
                for (const auto& cb : s.codeBlocks) {
                    sJson["codeBlocks"].push_back({
                        {"filename", cb.filename},
                        {"language", cb.language},
                        {"code", cb.code}
                    });
                }

                // Charts
                sJson["charts"] = json::array();
                for (const auto& chart : s.charts) {
                    json cJson;
                    cJson["title"] = chart.title;
                    cJson["type"]  = (chart.type == ChartData::Type::Bar) ? "Bar" : 
                                     (chart.type == ChartData::Type::Line) ? "Line" : "Pie";
                    sJson["charts"].push_back(cJson);
                }

                templateData["sections"].push_back(sJson);
            }

            // References
            templateData["references"] = content.references;

            std::string renderedHtml = content.renderedHtml.empty()
                ? TemplateEngine::render(tmplContent, templateData)
                : content.renderedHtml;
            
            // Save HTML and Convert to PDF
            updateStatus("Converting HTML structure to PDF...");
            std::string htmlFilename = utils::generateFilename("report", "html");
            std::string htmlPath = outputDir + "/" + htmlFilename;
            utils::writeFile(htmlPath, renderedHtml);

            std::string pdfFilename = utils::generateFilename("report", "pdf");
            std::string pdfPath = outputDir + "/" + pdfFilename;

            bool ok = HtmlConverter::convertToPdf(htmlPath, pdfPath);
            
            if (!ok) {
                std::cerr << "[Server] HTML to PDF failed, falling back to native writer..." << std::endl;
                PdfEngine nativeEngine;
                pdfPath = nativeEngine.generateReport(content, style, outputDir);
            }

            // ── Step 5: Extract Assets (Code files)
            updateStatus("Extracting source code assets...");
            
            auto ensureExt = [](const std::string& fname, const std::string& lang) {
                std::string base = fname.empty() ? "script" : fname;
                if (base.find('.') != std::string::npos) return base;
                std::string l = utils::toLower(lang);
                if (l == "python" || l == "py") return base + ".py";
                if (l == "cpp" || l == "c++" || l == "cplusplus") return base + ".cpp";
                if (l == "js" || l == "javascript") return base + ".js";
                if (l == "html") return base + ".html";
                if (l == "css") return base + ".css";
                if (l == "java") return base + ".java";
                if (l == "c") return base + ".c";
                if (l == "markdown" || l == "md") return base + ".md";
                return base + ".txt";
            };

            std::vector<std::string> savedAssets;
            std::map<std::string, int> nameCounts;
            auto saveAsset = [&](const CodeBlock& cb) {
                if (cb.code.empty()) {
                    return;
                }

                std::string safeName = ensureExt(cb.filename, cb.language);
                if (nameCounts.count(safeName)) {
                    size_t dot = safeName.find_last_of('.');
                    std::string name = (dot == std::string::npos) ? safeName : safeName.substr(0, dot);
                    std::string ext = (dot == std::string::npos) ? "" : safeName.substr(dot);
                    safeName = name + "_" + std::to_string(++nameCounts[safeName]) + ext;
                } else {
                    nameCounts[safeName] = 0;
                }

                std::string assetPath = outputDir + "/" + safeName;
                utils::writeFile(assetPath, cb.code);
                savedAssets.push_back(safeName);
            };

            for (const auto& asset : content.assets) {
                saveAsset(asset);
            }
            for (const auto& sec : content.sections) {
                for (const auto& cb : sec.codeBlocks) {
                    saveAsset(cb);
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            updateStatus("Completed in " + std::to_string(ms) + " ms");

            // ── Finalize
            {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                auto& s = sessions_[sessionId];
                if (pdfPath.empty() || !fs::exists(pdfPath)) {
                    s.isError = true;
                    s.status = "Failed to generate PDF.";
                } else {
                    s.isComplete = true;
                    s.reportTitle = content.title;
                    s.pdfUrl = "/outputs/" + fs::path(pdfPath).filename().string();
                    s.assets = savedAssets;
                }
            }
        }).detach();

        // Return immediately
        json response = {
            {"sessionId", sessionId},
            {"status", "started"}
        };
        res.set_content(response.dump(), "application/json");
    });

    // Asset management
    svr.Get("/api/assets", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("session_id")) {
            res.status = 400;
            res.set_content(json({{"error", "Missing session_id"}}).dump(), "application/json");
            return;
        }
        std::string sessionId = req.get_param_value("session_id");

        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            res.status = 404;
            res.set_content(json({{"error", "Session not found"}}).dump(), "application/json");
            return;
        }
        if (!canAccessSession(req, it->second)) {
            res.status = 403;
            res.set_content(json({{"error", "Forbidden"}}).dump(), "application/json");
            return;
        }

        json arr = json::array();
        for (auto& a : it->second.assets) arr.push_back(a);
        res.set_content(arr.dump(), "application/json");
    });

    svr.Get("/api/download", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("session_id") || !req.has_param("file")) {
            res.status = 400;
            res.set_content("Missing params", "text/plain");
            return;
        }
        std::string sessionId = req.get_param_value("session_id");
        std::string file      = req.get_param_value("file");

        SessionState state;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            auto it = sessions_.find(sessionId);
            if (it == sessions_.end()) {
                res.status = 404;
                res.set_content("Session not found", "text/plain");
                return;
            }
            if (!canAccessSession(req, it->second)) {
                res.status = 403;
                res.set_content("Forbidden", "text/plain");
                return;
            }
            state = it->second;
        }

        if (std::find(state.assets.begin(), state.assets.end(), file) == state.assets.end()) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return;
        }

        fs::path resolvedPath = fs::weakly_canonical(outputRoot / file);
        const std::string resolvedText = resolvedPath.string();
        const std::string outputRootText = outputRoot.string();
        if (resolvedText.rfind(outputRootText, 0) != 0 || !utils::fileExists(resolvedText)) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return;
        }

        auto bytes = utils::readBinaryFile(resolvedText);
        std::string content(bytes.begin(), bytes.end());
        res.set_content(content, "application/octet-stream");
        res.set_header("Content-Disposition", "attachment; filename=\"" + file + "\"");
    });

    // SSE Stream
    svr.Get("/api/stream", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("session_id")) {
            res.status = 400;
            res.set_content("Missing session_id", "text/plain");
            return;
        }
        std::string sessionId = req.get_param_value("session_id");

        res.set_chunked_content_provider("text/event-stream",
            [this, sessionId](size_t /*offset*/, httplib::DataSink &sink) {
                SessionState currentState;
                {
                    std::lock_guard<std::mutex> lock(sessionsMutex_);
                    auto it = sessions_.find(sessionId);
                    if (it != sessions_.end()) {
                        currentState = it->second;
                    } else {
                        currentState.isError = true;
                        currentState.status = "Session not found.";
                    }
                }

                json data = {
                    {"status", currentState.status},
                    {"isComplete", currentState.isComplete},
                    {"isError", currentState.isError},
                    {"pdfUrl", currentState.pdfUrl},
                    {"reportTitle", currentState.reportTitle}
                };

                std::string eventPayload = "data: " + data.dump() + "\n\n";
                sink.write(eventPayload.c_str(), eventPayload.size());

                if (currentState.isComplete || currentState.isError) {
                    sink.done();
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return true;
            });
    });


    // Download saved PDFs
    svr.set_mount_point("/outputs", outputDir);

    // Start server
    std::cout << "\n"
              << "==========================================================\n"
              << " PROJECT ELPIS\n"
              << "==========================================================\n"
              << "  Server:  http://localhost:" << port_ << "\n"
              << "  Static:  " << staticDir << "\n"
              << "  Output:  " << outputDir << "\n"
              << "  API Key: " << (apiKey_.empty() ? "Not set (demo mode)" : "Configured") << "\n"
              << "==========================================================\n"
              << std::endl;

    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << "[Error] Failed to start server on port " << port_ << std::endl;
    }
}
