cmake_minimum_required(VERSION 3.24)
project(NotchGlass VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "NotchGlass targets Windows 10/11 (Win32 + DirectComposition + C++/WinRT) exclusively.")
endif()

# Compile HLSL Pixel Shader for Direct2D Custom Effect (ps_4_0_level_9_3 / ps_5_0)
find_program(FXC_EXECUTABLE fxc HINTS
    "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
)

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/LiquidGlassPS.hlsl")
set(SHADER_BIN "${CMAKE_CURRENT_BINARY_DIR}/LiquidGlassPS.cso")
set(SHADER_HDR "${CMAKE_CURRENT_BINARY_DIR}/generated/LiquidGlassShaderBytecode.hpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(FXC_EXECUTABLE)
    add_custom_command(
        OUTPUT "${SHADER_HDR}"
        COMMAND "${FXC_EXECUTABLE}" /T ps_4_0 /E main /Vn g_LiquidGlassPSBytecode /Fh "${SHADER_HDR}" "${SHADER_SRC}"
        DEPENDS "${SHADER_SRC}"
        COMMENT "Compiling LiquidGlassPS.hlsl -> C++ header bytecode"
    )
else()
    message(WARNING "fxc.exe not found; runtime D3DCompile fallback enabled for LiquidGlassPS.hlsl")
endif()

set(NOTCHGLASS_SOURCES
    src/main.cpp
    src/window/SlimLineWindow.cpp
    src/graphics/LiquidGlassEngine.cpp
    src/modules/MediaSmtcModule.cpp
    src/modules/MicAudioModule.cpp
    src/modules/FileShelfModule.cpp
    src/modules/QrWifiTransferModule.cpp
    src/modules/ClipboardModule.cpp
    src/modules/QuickToolsModule.cpp
    src/modules/FocusModule.cpp
    $<$<BOOL:${FXC_EXECUTABLE}>:${SHADER_HDR}>
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/notchglass.ico")
    list(APPEND NOTCHGLASS_SOURCES src/resources/notchglass.rc)
    set(HAS_NOTCHGLASS_ICON 1)
else()
    set(HAS_NOTCHGLASS_ICON 0)
endif()

add_executable(NotchGlass WIN32 ${NOTCHGLASS_SOURCES})

target_include_directories(NotchGlass PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(NotchGlass PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    $<$<BOOL:${FXC_EXECUTABLE}>:NOTCHGLASS_HAS_PRECOMPILED_SHADER=1>
    $<$<BOOL:${HAS_NOTCHGLASS_ICON}>:NOTCHGLASS_HAS_ICON=1>
)

if(MSVC)
    target_compile_options(NotchGlass PRIVATE
        /W4
        /permissive-
        /utf-8
        /EHsc
        /O2
        /Gw
        /Gy
        /DNDEBUG
    )
    target_link_options(NotchGlass PRIVATE
        /SUBSYSTEM:WINDOWS
        /OPT:REF
        /OPT:ICF
        /INCREMENTAL:NO
    )
endif()

# Native Windows System & Graphics Libraries (Zero Electron/WebView/CLR)
target_link_libraries(NotchGlass PRIVATE
    user32
    gdi32
    shell32
    comdlg32
    ole32
    oleaut32
    shlwapi
    dwmapi
    d3d11
    d2d1
    dwrite
    dcomp
    dxgi
    dxguid
    d3dcompiler
    windowscodecs
    windowsapp
    ws2_32
    iphlpapi
    pdh
    mmdevapi
    propsys
    powrprof
    dxva2
    wbemuuid
    mfplat
    mfreadwrite
    mfuuid
)

# Copy shader source alongside binary for optional hot-compile fallback
add_custom_command(TARGET NotchGlass POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADER_SRC}"
        "$<TARGET_FILE_DIR:NotchGlass>/LiquidGlassPS.hlsl"
)
