cmake_minimum_required(VERSION 3.19)

project(GigabyteRGBController
    VERSION 0.1.0
    DESCRIPTION "RGB Fusion 2.0 lighting control for Gigabyte motherboards on Linux"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 QuickLayouts)

qt_standard_project_setup(REQUIRES 6.5)

# The QML files live in qml/ rather than at the module root, so the module needs
# a qmldir generated for that subdirectory too. Without it the singleton
# declaration for Theme is only visible in the root qmldir, and any file
# resolving Theme through the implicit directory import gets it as a plain
# component - every Theme.* read then comes back undefined.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(GigabyteRGBController
    src/main.cpp
    src/HidRawDevice.cpp
    src/HidRawDevice.h
    src/RgbFusion2.cpp
    src/RgbFusion2.h
    src/LampArray.cpp
    src/LampArray.h
    src/Config.cpp
    src/Config.h
    src/Controller.cpp
    src/Controller.h
)

# Theme is a QML singleton; without this the generated qmldir omits the
# `singleton` keyword and every `Theme.x` reference fails to resolve.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The QML import scan exists to link static QML plugins into the binary. Against
# a shared Qt those plugin targets are not exported at all, so the scan finds
# nothing to link and only emits a warning per plugin - skip it there, but keep
# it for a static Qt, where it is load-bearing.
if(QT6_IS_SHARED_LIBS_BUILD)
    set(grc_import_scan NO_IMPORT_SCAN)
endif()

qt_add_qml_module(GigabyteRGBController
    URI GigabyteRGBController
    VERSION 1.0
    ${grc_import_scan}
    # The module's build-tree directory defaults to the URI, which here is also
    # the executable's name - the linker then refuses to write the binary over a
    # directory. Only the build tree moves; the resource path the runtime
    # imports from is derived from the URI and is unaffected.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml_module/GigabyteRGBController
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/TitleBar.qml
        qml/WindowButton.qml
        qml/WindowResizeBorder.qml
        qml/Card.qml
        qml/ModalCard.qml
        qml/PillButton.qml
        qml/ToggleSwitch.qml
        qml/ToolTipBubble.qml
        qml/LabelledSlider.qml
        qml/EffectGlyph.qml
        qml/EffectButton.qml
        qml/EffectRail.qml
        qml/EffectSettings.qml
        qml/ColorWheel.qml
        qml/ColorFields.qml
        qml/Swatches.qml
        qml/ZoneBar.qml
        qml/LampArrayPage.qml
)

target_link_libraries(GigabyteRGBController PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

target_compile_options(GigabyteRGBController PRIVATE -Wall -Wextra)

install(TARGETS GigabyteRGBController RUNTIME DESTINATION bin)
install(FILES udev/99-rgbfusion2.rules DESTINATION lib/udev/rules.d)
install(FILES desktop/GigabyteRGBController.desktop DESTINATION share/applications)

# ---- tests ------------------------------------------------------------------
# Protocol-only; they need neither a display nor the hardware.
option(BUILD_TESTING "Build the protocol tests" ON)

if(BUILD_TESTING)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_executable(tst_protocol
            tests/tst_protocol.cpp
            src/RgbFusion2.cpp
            src/LampArray.cpp
            src/HidRawDevice.cpp
        )
        target_link_libraries(tst_protocol PRIVATE Qt6::Test Qt6::Gui)
        target_compile_options(tst_protocol PRIVATE -Wall -Wextra)
        add_test(NAME protocol COMMAND tst_protocol)

        # Config touches only QSettings and runs with QStandardPaths in test
        # mode, so it needs neither the hardware nor the user's real config.
        add_executable(tst_config
            tests/tst_config.cpp
            src/Config.cpp
            src/RgbFusion2.cpp
            src/HidRawDevice.cpp
        )
        target_link_libraries(tst_config PRIVATE Qt6::Test Qt6::Gui)
        target_compile_options(tst_config PRIVATE -Wall -Wextra)
        add_test(NAME config COMMAND tst_config)
    else()
        message(STATUS "Qt6Test not found - skipping tests")
    endif()
endif()
