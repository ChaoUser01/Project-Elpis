#include "server.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>

// Simple .env parser
void loadEnv(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            _putenv_s(key.c_str(), val.c_str());
        }
    }
}

int main(int argc, char* argv[]) {
    Server server;

    // Load .env if present
    loadEnv(utils::getExecutableDir() + "/../.env");
    loadEnv(".env");

    // Parse command-line arguments
    int port = 8000;
    std::string apiKey;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
        else if ((arg == "--api-key" || arg == "-k") && i + 1 < argc) {
            apiKey = argv[++i];
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Elpis - Shall Bring an End to All Of It\n\n"
                      << "Usage: elpis_engine [OPTIONS]\n\n"
                      << "Options:\n"
                      << "  -p, --port <PORT>      Server port (default: 8000)\n"
                      << "  -k, --api-key <KEY>    OpenAI API key (or set OPENAI_API_KEY env)\n"
                      << "  -h, --help             Show this help\n\n"
                      << "Without an API key, the system runs in demo mode with\n"
                      << "pre-built academic content.\n";
            return 0;
        }
    }

    // Environment variable fallback for API key
    if (apiKey.empty()) {
        const char* envKey = std::getenv("OPENAI_API_KEY");
        if (envKey) apiKey = envKey;
    }

    // Create required directories
    std::string baseDir = utils::getExecutableDir();
    utils::ensureDir(baseDir + "/outputs");
    utils::ensureDir(baseDir + "/assets");

    server.setPort(port);
    if (!apiKey.empty()) {
        server.setApiKey(apiKey);
    }

    server.start();
    return 0;
}
