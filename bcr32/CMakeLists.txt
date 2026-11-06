# SPDX-License-Identifier: AGPL-3.0-or-later
# Behringer's BCR32 demo firmware (App/ and the SSD1306 driver), unchanged, against the desktop board
# in shim/ (in place of demo/Board, which is written against the GigaDevice library).
set(BCR32_DEMO ${BCR32_SOURCE}/demo)
set(BCR32_APP main input cv_out sysex activity_led sync_loop midi_thru flash_test)
set(BCR32_FW_SOURCES ${BCR32_DEMO}/ThirdParty/ssd1306/ssd1306.c ${BCR32_DEMO}/ThirdParty/ssd1306/ssd1306_fonts.c)
foreach(f ${BCR32_APP})
    list(APPEND BCR32_FW_SOURCES ${BCR32_DEMO}/App/${f}.c)
endforeach()
foreach(f ${BCR32_FW_SOURCES} ${BCR32_DEMO}/Board/bsp_gpio.h)
    if(NOT EXISTS ${f})
        message(FATAL_ERROR "BCR32_SOURCE=${BCR32_SOURCE} has no ${f}")
    endif()
endforeach()

add_library(bcr32-firmware STATIC ${BCR32_FW_SOURCES} shim/hw.c)
# shim/include first: it stands in for the GigaDevice headers and the USB stack (not compiled)
target_include_directories(bcr32-firmware PUBLIC shim
                           PRIVATE shim/include ${BCR32_DEMO}/App ${BCR32_DEMO}/Board ${BCR32_DEMO}/ThirdParty/ssd1306)
set_source_files_properties(${BCR32_DEMO}/App/main.c PROPERTIES COMPILE_DEFINITIONS main=bcr32_firmware_main)
target_compile_options(bcr32-firmware PRIVATE -w)

juce_add_gui_app(bcr32-emulator PRODUCT_NAME "BCR32 emulator" COMPANY_NAME "behringer-desktop-emulators")
target_sources(bcr32-emulator PRIVATE panel/Main.cpp)
target_compile_definitions(bcr32-emulator PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="BCR32 emulator"
                                                  JUCE_APPLICATION_VERSION_STRING="0.2.0")
target_link_libraries(bcr32-emulator PRIVATE bcr32-firmware juce::juce_gui_basics juce::juce_audio_devices
                                             juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
