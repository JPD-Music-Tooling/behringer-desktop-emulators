# SPDX-License-Identifier: AGPL-3.0-or-later
# Behringer's AQ64L demo firmware, unchanged, against the desktop hardware in shim/.
set(AQ64L_DEMO ${AQ64L_SOURCE}/demo)
foreach(f Template/main.c Demo/midi_demo.c Userdrv/rgb.h Userdrv/button.h)
    if(NOT EXISTS ${AQ64L_DEMO}/${f})
        message(FATAL_ERROR "AQ64L_SOURCE=${AQ64L_SOURCE} has no demo/${f}")
    endif()
endforeach()

add_library(aq64l-firmware STATIC ${AQ64L_DEMO}/Template/main.c ${AQ64L_DEMO}/Demo/midi_demo.c shim/hw.c)
# shim/include first: it stands in for the GigaDevice headers and the USB stack (not compiled)
target_include_directories(aq64l-firmware PUBLIC shim PRIVATE shim/include ${AQ64L_DEMO}/Template ${AQ64L_DEMO}/Userdrv ${AQ64L_DEMO}/Demo)
set_source_files_properties(${AQ64L_DEMO}/Template/main.c PROPERTIES COMPILE_DEFINITIONS main=aq64l_firmware_main)
target_compile_options(aq64l-firmware PRIVATE -w)

juce_add_gui_app(aq64l-emulator PRODUCT_NAME "AQ64L emulator" COMPANY_NAME "behringer-desktop-emulators")
target_sources(aq64l-emulator PRIVATE panel/Main.cpp)
target_compile_definitions(aq64l-emulator PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="AQ64L emulator"
                                                  JUCE_APPLICATION_VERSION_STRING="0.2.0")
target_link_libraries(aq64l-emulator PRIVATE aq64l-firmware juce::juce_gui_basics juce::juce_audio_devices
                                             juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
