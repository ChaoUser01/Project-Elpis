#include "ocr_engine.h"

#include <windows.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// Tesseract C API for dynamic loading

typedef void* TessBaseAPI;
typedef void* TessPix;

typedef TessBaseAPI (*TessBaseAPICreate_t)();
typedef int   (*TessBaseAPIInit3_t)(TessBaseAPI, const char*, const char*);
typedef void  (*TessBaseAPISetImage2_t)(TessBaseAPI, TessPix);
typedef void  (*TessBaseAPISetImage_t)(TessBaseAPI, const unsigned char*, int, int, int, int);
typedef char* (*TessBaseAPIGetUTF8Text_t)(TessBaseAPI);
typedef void  (*TessBaseAPIEnd_t)(TessBaseAPI);
typedef void  (*TessBaseAPIDelete_t)(TessBaseAPI);
typedef void  (*TessDeleteText_t)(char*);

// Leptonica (used by Tesseract for image loading)
typedef TessPix (*pixReadMem_t)(const unsigned char*, size_t);
typedef void    (*pixDestroy_t)(TessPix*);

// Implementation

OcrEngine::OcrEngine() {
    // Try to load Tesseract DLL dynamically
    HMODULE hTess = LoadLibraryA("tesseract53.dll");
    if (!hTess) hTess = LoadLibraryA("tesseract50.dll");
    if (!hTess) hTess = LoadLibraryA("tesseract41.dll");
    if (!hTess) hTess = LoadLibraryA("libtesseract-5.dll");
    if (!hTess) hTess = LoadLibraryA("libtesseract.dll");

    if (!hTess) {
        // Try common installation paths
        const char* paths[] = {
            "C:\\Program Files\\Tesseract-OCR\\libtesseract-5.dll",
            "C:\\Program Files\\Tesseract-OCR\\tesseract53.dll",
            "C:\\Program Files (x86)\\Tesseract-OCR\\libtesseract-5.dll",
        };
        for (auto& p : paths) {
            if (fs::exists(p)) {
                hTess = LoadLibraryA(p);
                if (hTess) break;
            }
        }
    }

    if (hTess) {
        auto createFn = (TessBaseAPICreate_t)GetProcAddress(hTess, "TessBaseAPICreate");
        auto initFn   = (TessBaseAPIInit3_t)GetProcAddress(hTess, "TessBaseAPIInit3");

        if (createFn && initFn) {
            TessBaseAPI handle = createFn();
            if (handle) {
                int rc = initFn(handle, nullptr, "eng");
                if (rc == 0) {
                    api_ = handle;
                    hModule_ = hTess;
                    available_ = true;
                }
            }
        }
    }
}

OcrEngine::~OcrEngine() {
    if (api_ && hModule_) {
        auto endFn = (TessBaseAPIEnd_t)GetProcAddress((HMODULE)hModule_, "TessBaseAPIEnd");
        auto delFn = (TessBaseAPIDelete_t)GetProcAddress((HMODULE)hModule_, "TessBaseAPIDelete");
        if (endFn) endFn(api_);
        if (delFn) delFn(api_);
    }
    if (hModule_) {
        FreeLibrary((HMODULE)hModule_);
    }
}

std::string OcrEngine::extractText(const std::vector<uint8_t>& imageBytes) {
    if (!available_ || !api_ || !hModule_) {
        return "[OCR Error] Tesseract is not available. Please install Tesseract-OCR "
               "or provide a text prompt instead.";
    }

    HMODULE hTess = (HMODULE)hModule_;

    // Try to use Leptonica's pixReadMem to load the image
    HMODULE hLept = LoadLibraryA("leptonica-1.84.1.dll");
    if (!hLept) hLept = LoadLibraryA("libleptonica.dll");
    if (!hLept) hLept = LoadLibraryA("liblept-5.dll");

    if (hLept) {
        auto pixReadMemFn = (pixReadMem_t)GetProcAddress(hLept, "pixReadMem");
        auto pixDestroyFn = (pixDestroy_t)GetProcAddress(hLept, "pixDestroy");
        auto setImage2Fn  = (TessBaseAPISetImage2_t)GetProcAddress(hTess, "TessBaseAPISetImage2");
        auto getTextFn    = (TessBaseAPIGetUTF8Text_t)GetProcAddress(hTess, "TessBaseAPIGetUTF8Text");
        auto delTextFn    = (TessDeleteText_t)GetProcAddress(hTess, "TessDeleteText");

        if (pixReadMemFn && setImage2Fn && getTextFn) {
            TessPix pix = pixReadMemFn(imageBytes.data(), imageBytes.size());
            if (pix) {
                setImage2Fn(api_, pix);
                char* text = getTextFn(api_);
                std::string result = text ? text : "";
                if (text && delTextFn) delTextFn(text);
                if (pixDestroyFn) pixDestroyFn(&pix);
                FreeLibrary(hLept);
                return result;
            }
        }
        FreeLibrary(hLept);
    }

    return "[OCR Error] Could not process image. Leptonica library not found.";
}
