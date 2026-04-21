cmake_minimum_required(VERSION 3.14)
project(Elpis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch header-only dependencies ──────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(httplib json)

# ── Source files ────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/server.cpp
    src/agent.cpp
    src/http_client.cpp
    src/pdf_writer.cpp
    src/pdf_engine.cpp
    src/chart_renderer.cpp
    src/style_profile.cpp
    src/ocr_engine.cpp
    src/template_engine.cpp
    src/base64.cpp
    src/utils.cpp
    src/user_ledger.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        winhttp          # Windows native HTTPS
        shlwapi          # Path utilities
        bcrypt           # Windows CryptoAPI (SHA-256)
)

# ── Copy static assets to build directory ───────────────────────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/static"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/static"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/templates"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/templates"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/outputs"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)
