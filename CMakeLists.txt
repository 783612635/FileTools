cmake_minimum_required(VERSION 3.20)
project(FileTools VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch Dear ImGui (docking branch for panel layout)
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Fetch spdlog — fast C++ logging library (header-only)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# ImGui core + backends source files
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# Our source files
file(GLOB_RECURSE OUR_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
)

# Windows resource file (embeds app icon into exe)
set(RESOURCE_FILE ${CMAKE_SOURCE_DIR}/src/resource.rc)

# Windows executable
add_executable(${PROJECT_NAME} WIN32
    ${OUR_SOURCES}
    ${IMGUI_SOURCES}
    ${RESOURCE_FILE}
)

# Include paths
target_include_directories(${PROJECT_NAME} PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src
)

# Link DirectX 11 + Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    d3d11
    dxgi
    d3dcompiler
    dxguid
    winmm
    imm32
    version
    gdiplus
)

# Windows-specific compiler flags
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# Enable multi-processor compilation for speed
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /MP /utf-8)
endif()

# Output to bin/
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy icon PNG and create logs dir next to exe
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/res/filetool.png"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/filetool.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/logs"
    COMMENT "Copying resources..."
)

# ── 7za.exe ──────────────────────────────────────────────────────
set(SEVENZIP_EXE "${CMAKE_SOURCE_DIR}/tools/7za.exe")
set(SEVENZIP_URL "https://www.7-zip.org/a/7z2409-extra.7z")
set(SEVENZIP_TMP "${CMAKE_BINARY_DIR}/7z-extra-tmp.7z")

if(NOT EXISTS "${SEVENZIP_EXE}")
    message(STATUS "7za.exe not found — downloading from 7-zip.org...")
    file(DOWNLOAD "${SEVENZIP_URL}" "${SEVENZIP_TMP}"
         STATUS DL_STATUS TIMEOUT 60)
    list(GET DL_STATUS 0 DL_CODE)
    if(DL_CODE EQUAL 0)
        # Extract 7za.exe from the .7z using CMake+tar (tar can't handle .7z)
        # Workaround: download 7zr.exe (light version) first, then use it
        set(SEVENZIP_LIGHT "${CMAKE_BINARY_DIR}/7zr.exe")
        file(DOWNLOAD "https://www.7-zip.org/a/7zr.exe" "${SEVENZIP_LIGHT}"
             STATUS DL2_STATUS TIMEOUT 30)
        list(GET DL2_STATUS 0 DL2_CODE)
        if(DL2_CODE EQUAL 0 AND EXISTS "${SEVENZIP_LIGHT}")
            execute_process(
                COMMAND "${SEVENZIP_LIGHT}" x "${SEVENZIP_TMP}" -o"${CMAKE_SOURCE_DIR}/tools/" 7za.exe -y
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(EXISTS "${SEVENZIP_EXE}")
                message(STATUS "7za.exe extracted successfully")
            else()
                message(WARNING "7za.exe extraction failed — place it manually in tools/")
            endif()
            file(REMOVE "${SEVENZIP_LIGHT}")
        else()
            message(WARNING "Cannot download extractor — place 7za.exe manually in tools/")
        endif()
        file(REMOVE "${SEVENZIP_TMP}")
    else()
        message(WARNING "Download failed — place 7za.exe manually in ${CMAKE_SOURCE_DIR}/tools/")
        message(WARNING "URL: ${SEVENZIP_URL}")
    endif()
endif()

if(EXISTS "${SEVENZIP_EXE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SEVENZIP_EXE}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/7za.exe"
        COMMENT "Copying 7za.exe"
    )
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/7z.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/tools/7z.dll" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/7z.dll"
        COMMENT "Copying 7z.dll"
    )
endif()
