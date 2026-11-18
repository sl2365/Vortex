cmake_minimum_required(VERSION 3.25)
project(Vortex VERSION 1.23.1 LANGUAGES C CXX)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../_Tools/JUCE/_9.0.4")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE 9.0.4 not found at: ${JUCE_DIR}")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(Vortex
    VERSION 1.23.1
    COMPANY_NAME "Dead Channel Audio"
    BUNDLE_ID "com.sl23.vortex"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE sl23
    PLUGIN_CODE vtx1
    FORMATS VST3
    VST3_CATEGORIES Fx Modulation
    PRODUCT_NAME "Vortex")

juce_add_binary_data(VortexResources
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/../Resources/logo.svg")

juce_generate_juce_header(Vortex)
target_sources(Vortex PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    PluginEditor.cpp PluginEditor.h
    VortexSequencer.h)
target_compile_features(Vortex PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(Vortex PRIVATE /utf-8)
endif()
target_compile_definitions(Vortex PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Vortex PRIVATE juce::juce_audio_utils VortexResources
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
           juce::juce_recommended_lto_flags)
