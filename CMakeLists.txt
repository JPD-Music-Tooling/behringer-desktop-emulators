# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(BehringerDesktopEmulators VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# JUCE 7.0.12, the version the IGNITE app is built with. Point FETCHCONTENT_SOURCE_DIR_JUCE at a copy you
# have to skip the download.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 7.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

# Behringer's sources for the two controllers whose firmware is compiled into the emulator. `emu fetch`
# clones them and passes these; leave one empty to skip that emulator.
set(AQ64L_SOURCE "" CACHE PATH "checkout of MusicTribeOpenSource/0715-AAX (AQ64L)")
set(BCR32_SOURCE "" CACHE PATH "checkout of MusicTribeOpenSource/0715-AAW (BCR32)")

add_subdirectory(ignite)
add_subdirectory(tools/midiprobe)
if(AQ64L_SOURCE)
    add_subdirectory(aq64l)
endif()
if(BCR32_SOURCE)
    add_subdirectory(bcr32)
endif()
