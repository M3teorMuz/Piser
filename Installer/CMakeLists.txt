cmake_minimum_required(VERSION 3.22)

# Статическая среда выполнения: установщик запускается без VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(EXPitchSetup VERSION 0.5.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/payload.zip")
    message(FATAL_ERROR "Installer/payload.zip не найден: сначала соберите плагин и упакуйте EXPitch.vst3 в payload.zip")
endif()

# Всё, что нужно установщику, встраивается в сам exe: плагин (payload.zip), фон, шрифт Inter
juce_add_binary_data(SetupAssets SOURCES
    payload.zip
    ../Assets/bg.png
    ../Assets/noise.png
    ../Assets/Inter-Regular.otf
    ../Assets/Inter-Medium.otf)

juce_add_gui_app(EXPitchSetup
    PRODUCT_NAME "EXPitch_Setup"
    COMPANY_NAME "PitcherAudio"
    VERSION      ${PROJECT_VERSION})

target_sources(EXPitchSetup PRIVATE Main.cpp Admin.cpp)

target_compile_definitions(EXPitchSetup PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(EXPitchSetup
    PRIVATE
        SetupAssets
        juce::juce_gui_basics
        shell32
    PUBLIC
        juce::juce_recommended_config_flags)
