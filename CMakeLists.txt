# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 GiuxLab

cmake_minimum_required(VERSION 3.20)
project(GXWhiteboardStudio VERSION 0.1.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(CATALOG_FILES)
foreach(lang it en)
  set(mofile "${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}/LC_MESSAGES/gx-whiteboard-studio.mo")
  add_custom_command(OUTPUT "${mofile}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}/LC_MESSAGES"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_po.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/po/${lang}.po" "${mofile}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/${lang}.po"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_po.py"
    VERBATIM)
  list(APPEND CATALOG_FILES "${mofile}")
  install(FILES "${mofile}" DESTINATION "share/locale/${lang}/LC_MESSAGES")
endforeach()
add_custom_target(gxwb_catalogs ALL DEPENDS ${CATALOG_FILES})
add_executable(gx-whiteboard-studio
  src/main.cpp
  src/StudioWindow.h src/StudioWindow.cpp
  src/BoardView.h src/BoardView.cpp src/BoardImages.cpp
  src/CameraSource.h src/CameraSource.cpp
  src/NetworkSource.h src/NetworkSource.cpp
  src/LanCameraScanner.h src/LanCameraScanner.cpp
  src/LanScanDialog.h src/LanScanDialog.cpp
  src/RecordingEngine.h src/RecordingEngine.cpp
  src/RecordingDialog.h src/RecordingDialog.cpp
  src/ProjectDocument.h src/ProjectDocument.cpp
  src/Localization.h src/Localization.cpp)
target_link_libraries(gx-whiteboard-studio PRIVATE Qt6::Widgets Qt6::Network)
target_compile_options(gx-whiteboard-studio PRIVATE -Wall -Wextra -Wpedantic)
add_dependencies(gx-whiteboard-studio gxwb_catalogs)
install(TARGETS gx-whiteboard-studio RUNTIME DESTINATION bin)


# Debian F08 desktop integration, language catalogs and local documentation.
install(FILES packaging/gx-whiteboard-studio.desktop
        DESTINATION share/applications)
install(FILES packaging/gx-whiteboard-studio.xml
        DESTINATION share/mime/packages)
install(FILES packaging/gx-whiteboard-studio.svg
        DESTINATION share/icons/hicolor/scalable/apps)
install(FILES docs/manual_it.html docs/manual_en.html
        DESTINATION share/doc/gx-whiteboard-studio)

# Distribute the exact upstream license and Debian copyright metadata.
install(FILES LICENSE DESTINATION share/doc/gx-whiteboard-studio)
install(FILES packaging/copyright DESTINATION share/doc/gx-whiteboard-studio)
