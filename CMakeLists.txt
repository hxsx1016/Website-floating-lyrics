cmake_minimum_required(VERSION 3.16)
project(DesktopLyrics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(DesktopLyrics WIN32
    src/DesktopLyrics.cpp
    src/LrcParser.hpp
)

target_compile_definitions(DesktopLyrics PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(DesktopLyrics PRIVATE
    gdiplus
    dwmapi
    comctl32
    ole32
    shell32
    comdlg32
    user32
    gdi32
    wininet
    crypt32
    WindowsApp
    runtimeobject
)

if (MSVC)
    target_compile_options(DesktopLyrics PRIVATE /utf-8 /W3)
endif()

add_custom_command(TARGET DesktopLyrics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sample.lrc"
            "$<TARGET_FILE_DIR:DesktopLyrics>/sample.lrc"
)
