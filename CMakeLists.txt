cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel), macOS 10.13 minimum (a fixer AVANT project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(FreakOut VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows : runtime C++ statique (pas de redistribuable Visual C++ a installer)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE : dossier local si JUCE_DIR est fourni, sinon telechargement automatique
set(JUCE_DIR "" CACHE PATH "Copie locale de JUCE (optionnel)")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# visuel integre au plugin (aucun fichier externe a l'execution)
juce_add_binary_data(FreakOutAssets SOURCES
  background.jpg toggle_strip.png glow_strip.png fs_down.png
  assets/fonts/BarlowCondensed-SemiBold.ttf assets/fonts/BarlowCondensed-Bold.ttf)
set_target_properties(FreakOutAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_add_plugin(FreakOut
  COMPANY_NAME "Studio Wiga"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgFo
  BUNDLE_ID com.studiowiga.freakout
  FORMATS VST3
  VST3_CATEGORIES Fx Generator
  PRODUCT_NAME "FreakOut"
  VERSION 1.1.0
  DESCRIPTION "Natural harmonic feedback generator (FreqOut style) - single pedal format"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(FreakOut PRIVATE PluginProcessor.cpp PluginEditor.cpp)

if(MSVC)
  target_compile_options(FreakOut PRIVATE /utf-8)
endif()

target_compile_definitions(FreakOut PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FreakOut
  PRIVATE
    FreakOutAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
