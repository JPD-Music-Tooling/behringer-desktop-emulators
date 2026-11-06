# SPDX-License-Identifier: AGPL-3.0-or-later
# The IGNITE panel. The IGNITE app itself is Behringer's, built from 0716-AAF with patches/ (see emu).
juce_add_gui_app(ignite-panel PRODUCT_NAME "IGNITE panel" COMPANY_NAME "behringer-desktop-emulators")
target_sources(ignite-panel PRIVATE panel/Main.cpp panel/Panel.cpp panel/Link.cpp panel/ScreenView.cpp)
target_compile_definitions(ignite-panel PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="IGNITE panel"
                                                JUCE_APPLICATION_VERSION_STRING="0.2.0")
target_link_libraries(ignite-panel PRIVATE juce::juce_gui_basics juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
