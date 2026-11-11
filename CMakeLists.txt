cmake_minimum_required(VERSION 3.20)
# The app version lives in VERSION (X.Y.Z). Bumping it on master makes the
# release workflow build and publish a new release.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" EE_VERSION)
string(STRIP "${EE_VERSION}" EE_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(EasyEffectsWindowsV2 VERSION ${EE_VERSION})

# GitHub repository the app checks for new releases ("owner/name").
set(EE_UPDATE_REPO "VoidDeficit/EasyEffects-Windows-Porting" CACHE STRING "GitHub repository checked for updates")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add external dependencies
# RNNoise (mumble-voip fork) ships no CMakeLists, so build it here.
# USE_MALLOC replaces the C99 VLAs that MSVC doesn't support.
file(GLOB RNNOISE_SOURCES CONFIGURE_DEPENDS external/rnnoise/src/*.c)
add_library(rnnoise STATIC ${RNNOISE_SOURCES})
target_include_directories(rnnoise PUBLIC external/rnnoise/include PRIVATE external/rnnoise/src)
target_compile_definitions(rnnoise PRIVATE USE_MALLOC _USE_MATH_DEFINES)
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(SOUNDSTRETCH OFF CACHE BOOL "" FORCE)
add_subdirectory(external/soundtouch)

# Add JUCE from the submodule directory
add_subdirectory("external/JUCE" JUCE)

# Add the plugin target
juce_add_plugin(EasyEffects
    COMPANY_NAME "WWMM"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Wwmm
    PLUGIN_CODE Eeff
    FORMATS Standalone VST3
    PRODUCT_NAME "EasyEffects"
    VERSION ${EE_VERSION}
    VST3_CAN_REPLACE_VST2 FALSE
)

# Add source files
target_sources(EasyEffects
    PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/EffectChain.cpp
    src/dsp/EffectRegistry.cpp
    src/dsp/GainModule.cpp
    src/dsp/CompressorModule.cpp
    src/dsp/GateModule.cpp
    src/dsp/EqualizerModule.cpp
    src/dsp/LimiterModule.cpp
    src/dsp/FilterModule.cpp
    src/dsp/DelayModule.cpp
    src/dsp/ReverbModule.cpp
    src/dsp/ExciterModule.cpp
    src/dsp/BassEnhancerModule.cpp
    src/dsp/DeesserModule.cpp
    src/dsp/ConvolverModule.cpp
    src/dsp/LevelMeterModule.cpp
    src/dsp/StereoToolsModule.cpp
    src/dsp/ExpanderModule.cpp
    src/dsp/CrusherModule.cpp
    src/dsp/MaximizerModule.cpp
    src/dsp/CrossfeedModule.cpp
    src/dsp/BassLoudnessModule.cpp
    src/dsp/CrystalizerModule.cpp
    src/dsp/AutoGainModule.cpp
    src/dsp/MultibandCompressorModule.cpp
    src/dsp/MultibandGateModule.cpp
    src/dsp/ChorusModule.cpp
    src/dsp/PhaserModule.cpp
    src/dsp/RNNoiseModule.cpp
    src/dsp/PitchShiftModule.cpp
    src/ui/EQPlotComponent.cpp
    src/ui/EQNodeComponent.cpp
    src/ui/EQNodeSettingsPanel.cpp
    src/ui/DynamicsVisualizer.cpp
    src/ui/VisualEqualizerEditor.cpp
    src/ui/ConvolverEditor.cpp
    src/ui/GenericModuleEditor.cpp
    src/ui/WindowsChrome.cpp
    src/app/Updater.cpp
    src/PresetManager.cpp
    src/audio/WindowsAudioEndpoints.cpp
    src/audio/SecondaryPipeline.cpp
    src/audio/AsioHost.cpp
    src/audio/SarAutoSetup.cpp
    src/audio/WindowsAudioInput.cpp
    src/audio/WindowsAudioOutput.cpp
)

# Link JUCE modules
target_link_libraries(EasyEffects
    PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    rnnoise
    SoundTouch
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Add include directories
target_include_directories(EasyEffects PRIVATE src)

# Add compile definitions
target_compile_definitions(EasyEffects PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(EasyEffects PRIVATE EE_UPDATE_REPO="${EE_UPDATE_REPO}")

# ASIO support (lets EasyEffects host ASIO drivers such as Synchronous Audio
# Router). JUCE bundles the Steinberg ASIO interface headers, which Steinberg
# licenses under GPLv3 (this project's licence) or its proprietary licence.
if(WIN32)
    target_compile_definitions(EasyEffects PUBLIC JUCE_ASIO=1)
endif()

# Generate JuceHeader.h
juce_generate_juce_header(EasyEffects)

