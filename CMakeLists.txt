cmake_minimum_required(VERSION 3.16)
project(SpaceViewer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(MSVC)
  # Static CRT so the Windows build does not depend on the Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(spaceviewer
  src/main.cpp
  src/util.cpp
  src/layout.cpp
  src/scan.cpp
  src/ui.cpp
  src/export.cpp
  src/selftest.cpp
  src/motion.cpp
  src/opencl_fill.cpp
)

target_include_directories(spaceviewer PRIVATE src)

if(WIN32)
  target_sources(spaceviewer PRIVATE src/platform/win/window.cpp src/platform/win/app.rc)
  set_target_properties(spaceviewer PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME SpaceViewer)
  target_compile_definitions(spaceviewer PRIVATE _WIN32_WINNT=0x0601 WINVER=0x0601 UNICODE _UNICODE NOMINMAX)
  if(MSVC)
    target_compile_options(spaceviewer PRIVATE /utf-8 /W3)
    target_link_options(spaceviewer PRIVATE "/MANIFEST:NO")
  endif()
  if(MINGW)
    set(manifest_dir "${CMAKE_CURRENT_BINARY_DIR}/no-default-manifest")
    file(MAKE_DIRECTORY "${manifest_dir}")
    add_custom_command(
      OUTPUT "${manifest_dir}/default-manifest.o"
      COMMAND "${CMAKE_C_COMPILER}" -c "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/empty_manifest.c" -o "${manifest_dir}/default-manifest.o"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/empty_manifest.c"
      VERBATIM)
    add_custom_target(spaceviewer_manifest DEPENDS "${manifest_dir}/default-manifest.o")
    add_dependencies(spaceviewer spaceviewer_manifest)
    target_link_options(spaceviewer PRIVATE -static -static-libgcc -static-libstdc++ "SHELL:-B ${manifest_dir}/" -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
  endif()
  target_link_libraries(spaceviewer PRIVATE comctl32 user32 gdi32 shell32 ole32 uuid comdlg32)
elseif(APPLE)
  target_sources(spaceviewer PRIVATE
    src/platform/apple/window.mm
    src/platform/apple/disk_space.mm
    src/platform/apple/scan_policy.cpp
    src/platform/apple/PrivacyInfo.xcprivacy)
  set_source_files_properties(src/platform/apple/window.mm src/platform/apple/disk_space.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set_source_files_properties(src/platform/apple/PrivacyInfo.xcprivacy
    PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(spaceviewer PROPERTIES
    OUTPUT_NAME SpaceViewer
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME SpaceViewer
    MACOSX_BUNDLE_GUI_IDENTIFIER com.spaceviewer.app
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/apple/Info.plist.in")
  target_link_libraries(spaceviewer PRIVATE "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics" "-framework CoreServices")
else()
  target_sources(spaceviewer PRIVATE
    src/platform/linux/main.cpp
    src/platform/linux/host.cpp
    src/platform/linux/menu.cpp
    src/platform/linux/x11.cpp
    src/platform/linux/wayland.cpp)
  find_package(PkgConfig REQUIRED)
  find_package(X11 REQUIRED)
  find_path(XFT_INCLUDE_DIR X11/Xft/Xft.h)
  find_library(XFT_LIBRARY Xft)
  if(NOT XFT_INCLUDE_DIR OR NOT XFT_LIBRARY)
    message(FATAL_ERROR "libXft development files are required (Ubuntu: libxft-dev)")
  endif()
  pkg_check_modules(WAYLAND REQUIRED wayland-client)
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
  pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
  pkg_check_modules(FREETYPE REQUIRED freetype2)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  set(protocol_dir "${CMAKE_CURRENT_BINARY_DIR}/protocols")
  file(MAKE_DIRECTORY "${protocol_dir}")
  set(xdg_xml "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
  set(decoration_xml "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
  add_custom_command(
    OUTPUT "${protocol_dir}/xdg-shell-client-protocol.h" "${protocol_dir}/xdg-shell-protocol.c"
    COMMAND "${WAYLAND_SCANNER}" client-header "${xdg_xml}" "${protocol_dir}/xdg-shell-client-protocol.h"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xdg_xml}" "${protocol_dir}/xdg-shell-protocol.c"
    DEPENDS "${xdg_xml}" VERBATIM)
  add_custom_command(
    OUTPUT "${protocol_dir}/xdg-decoration-client-protocol.h" "${protocol_dir}/xdg-decoration-protocol.c"
    COMMAND "${WAYLAND_SCANNER}" client-header "${decoration_xml}" "${protocol_dir}/xdg-decoration-client-protocol.h"
    COMMAND "${WAYLAND_SCANNER}" private-code "${decoration_xml}" "${protocol_dir}/xdg-decoration-protocol.c"
    DEPENDS "${decoration_xml}" VERBATIM)
  target_sources(spaceviewer PRIVATE
    "${protocol_dir}/xdg-shell-protocol.c"
    "${protocol_dir}/xdg-decoration-protocol.c")
  target_include_directories(spaceviewer PRIVATE
    src/platform/linux
    "${protocol_dir}"
    ${X11_INCLUDE_DIR}
    ${XFT_INCLUDE_DIR}
    ${WAYLAND_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS})
  target_compile_definitions(spaceviewer PRIVATE _GNU_SOURCE)
  target_link_libraries(spaceviewer PRIVATE
    ${X11_LIBRARIES}
    ${XFT_LIBRARY}
    ${WAYLAND_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    ${FREETYPE_LIBRARIES})
  set_target_properties(spaceviewer PROPERTIES OUTPUT_NAME spaceviewer)
endif()

find_package(Threads REQUIRED)
target_link_libraries(spaceviewer PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
  target_link_libraries(spaceviewer PRIVATE stdc++fs)
endif()
