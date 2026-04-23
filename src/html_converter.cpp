#include "html_converter.h"
#include "utils.h"
#include <windows.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

static std::string fileUrlFromPath(const std::string& absPath) {
    // Convert Windows path to file:// URL for Chromium/Edge headless printing.
    // Example: C:\a\b\c.html -> file:///C:/a/b/c.html
    std::string p = absPath;
    std::replace(p.begin(), p.end(), '\\', '/');
    return "file:///" + p;
}

std::string HtmlConverter::getEdgePath() {
    // Standard Windows locations
    {
        std::string path = "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe";
        if (fs::exists(path)) return path;
    }
    {
        std::string path = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
        if (fs::exists(path)) return path;
    }
    
    // Check environment variable if custom path is needed
    const char* envPath = std::getenv("ELPIS_EDGE_PATH");
    if (envPath) return envPath;

    return "";
}

bool HtmlConverter::convertToPdf(const std::string& htmlPath, const std::string& pdfPath) {
    std::string edgePath = getEdgePath();
    if (edgePath.empty()) {
        std::cerr << "[HtmlConverter] MS Edge executable not found." << std::endl;
        return false;
    }

    // Convert to absolute paths to help Edge resolve local files reliably.
    const std::string absHtml = fs::absolute(fs::path(htmlPath)).string();
    const std::string absPdf  = fs::absolute(fs::path(pdfPath)).string();
    fs::create_directories(fs::path(absPdf).parent_path());

    const std::string htmlUrl = fileUrlFromPath(absHtml);

    // Prepare command line: msedge.exe --headless --disable-gpu --print-to-pdf="pdf" "html"
    // We use quotes for paths to handle spaces
    std::string cmd =
        "\"" + edgePath + "\""
        " --headless"
        " --disable-gpu"
        " --no-pdf-header-footer"
        " --print-to-pdf=\"" + absPdf + "\""
        " \"" + htmlUrl + "\"";

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcessA may modify the command line buffer; provide mutable storage.
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    // Create process headlessly
    if (!CreateProcessA(NULL, cmdline.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cerr << "[HtmlConverter] CreateProcess failed (" << GetLastError() << ")" << std::endl;
        return false;
    }

    // Wait up to 120 seconds for conversion (large reports can take time)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);
    
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (waitResult == WAIT_TIMEOUT) {
        std::cerr << "[HtmlConverter] Conversion timed out." << std::endl;
        return false;
    }

    if (exitCode != 0) {
        std::cerr << "[HtmlConverter] Conversion failed (exit code " << exitCode << ")." << std::endl;
    }

    return exitCode == 0 && fs::exists(absPdf) && fs::file_size(absPdf) > 0;
}
