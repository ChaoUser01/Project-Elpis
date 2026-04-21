#include "server.h"
#include "agent.h"
#include "ocr_engine.h"
#include "style_profile.h"
#include "pdf_engine.h"
#include "utils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

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
                {"claimed", user && !user->passwordHash.empty()},
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
            {"name", user ? user->name : ""},
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

        const AuthSession* session = ledger_.validateSession(auth.substr(7));
        if (!session) {
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
            const AuthSession* session = ledger_.validateSession(authToken);
            if (session) {
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

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_[sessionId] = SessionState();
        }

        // ── Spawn background thread
        std::thread([this, prompt, imageBytes, imageFilename, sessionId,
                     outputDir, effectiveApiKey, userProvider, styleSeed, userName]() mutable {
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
            ReportContent content = agent.generate(prompt);

            // Override author with authenticated user name if available
            if (!userName.empty()) {
                content.author = userName;
            }

            // ── Step 4: Generate PDF
            updateStatus("Rendering PDF document...");
            PdfEngine engine;
            std::string pdfPath = engine.generateReport(content, style, outputDir);

            // ── Step 5: Extract Assets (Code files)
            updateStatus("Extracting source code assets...");
            
            auto ensureExt = [](const std::string& fname, const std::string& lang) {
                if (fname.find('.') != std::string::npos) return fname;
                std::string l = utils::toLower(lang);
                if (l == "python" || l == "py") return fname + ".py";
                if (l == "cpp" || l == "c++" || l == "cplusplus") return fname + ".cpp";
                if (l == "js" || l == "javascript") return fname + ".js";
                if (l == "html") return fname + ".html";
                if (l == "css") return fname + ".css";
                if (l == "java") return fname + ".java";
                if (l == "c") return fname + ".c";
                return fname + ".txt";
            };

            std::vector<std::string> savedAssets;
            for (auto& sec : content.sections) {
                for (auto& cb : sec.codeBlocks) {
                    if (!cb.code.empty()) {
                        std::string safeName = ensureExt(cb.filename, cb.language);
                        std::string assetPath = outputDir + "/" + safeName;
                        utils::writeFile(assetPath, cb.code);
                        savedAssets.push_back(safeName);
                    }
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

        std::string path = baseDir_ + "/outputs/" + file;
        if (!utils::fileExists(path)) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return;
        }

        std::string content = utils::readFile(path);
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
