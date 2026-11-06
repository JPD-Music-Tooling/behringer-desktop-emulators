# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(IgniteEmulator VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 7.0.12, the version the IGNITE app is built with. Point FETCHCONTENT_SOURCE_DIR_JUCE at a copy you
# have to skip the download.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 7.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_gui_app(ignite-panel PRODUCT_NAME "IGNITE panel" COMPANY_NAME "IgniteEmulator")
target_sources(ignite-panel PRIVATE panel/Main.cpp panel/Panel.cpp panel/Link.cpp panel/ScreenView.cpp)
target_compile_definitions(ignite-panel PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="IGNITE panel"
                                                JUCE_APPLICATION_VERSION_STRING="0.1.0")
target_link_libraries(ignite-panel PRIVATE juce::juce_gui_basics juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
