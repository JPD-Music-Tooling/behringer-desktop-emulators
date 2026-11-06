# SPDX-License-Identifier: AGPL-3.0-or-later
juce_add_console_app(midiprobe PRODUCT_NAME "midiprobe")
target_sources(midiprobe PRIVATE Main.cpp)
target_compile_definitions(midiprobe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(midiprobe PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
