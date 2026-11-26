cmake_minimum_required(VERSION 3.24)
project(SpaceViewer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

if(MSVC)
  # Static CRT so the Windows build does not depend on the Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
option(SPACEVIEWER_BUILD_APP "Build the native desktop application" ON)

add_library(spaceviewer_core STATIC
  src/app/app.cpp
  src/app/commands.cpp
  src/app/edit.cpp
  src/app/input.cpp
  src/app/input_events.cpp
  src/app/tile_input.cpp
  src/model/tree.cpp
  src/model/filter.cpp
  src/model/category.cpp
  src/view/layout.cpp
  src/view/layout_cache.cpp
  src/scan/scanner.cpp
  src/scan/tasks.cpp
  src/scan/tree_updates.cpp
  src/io/export.cpp
  src/view/motion.cpp
  src/view/opencl_fill.cpp
  src/view/palette.cpp
  src/view/raster.cpp
  src/view/metrics.cpp
  src/view/icons.cpp
  src/view/render.cpp
  src/view/controls.cpp
  src/view/status.cpp
  src/view/treemap.cpp
  src/i18n/text.cpp
  src/i18n/format.cpp
  src/i18n/unicode.cpp
)

target_include_directories(spaceviewer_core PUBLIC src "${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
  target_sources(spaceviewer_core PRIVATE
    src/platform/win/encoding.cpp src/platform/win/scan.cpp src/platform/win/watch.cpp)
  target_compile_definitions(spaceviewer_core PUBLIC
    _WIN32_WINNT=0x0601 WINVER=0x0601 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    target_compile_options(spaceviewer_core PUBLIC /utf-8 /W3)
  endif()
elseif(APPLE)
  target_sources(spaceviewer_core PRIVATE
    src/platform/posix/volume.cpp src/platform/posix/scan.cpp
    src/platform/apple/disk_space.mm src/platform/apple/scan_policy.cpp src/platform/apple/watch.cpp)
  set_source_files_properties(src/platform/apple/disk_space.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(spaceviewer_core PUBLIC "-framework Cocoa" "-framework CoreServices")
else()
  target_sources(spaceviewer_core PRIVATE
    src/platform/posix/volume.cpp src/platform/posix/scan.cpp src/platform/linux/watch.cpp)
endif()

if(SPACEVIEWER_BUILD_APP OR BUILD_TESTING)
  add_library(spaceviewer_test_support STATIC tests/selftest.cpp)
  target_link_libraries(spaceviewer_test_support PUBLIC spaceviewer_core)
endif()

if(APPLE AND (SPACEVIEWER_BUILD_APP OR BUILD_TESTING))
  add_library(spaceviewer_apple_text STATIC src/platform/apple/text_renderer.mm)
  target_link_libraries(spaceviewer_apple_text PUBLIC spaceviewer_core "-framework CoreText")
  set_source_files_properties(src/platform/apple/text_renderer.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

if(SPACEVIEWER_BUILD_APP)
add_executable(spaceviewer src/app/main.cpp)
target_link_libraries(spaceviewer PRIVATE spaceviewer_test_support)

set(SPACEVIEWER_GENERATED_FONTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-fonts")
file(MAKE_DIRECTORY "${SPACEVIEWER_GENERATED_FONTS_DIR}")
set(SPACEVIEWER_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf")
foreach(font IN ITEMS SourceHanSansSC-Regular.otf SourceHanSansTC-Regular.otf)
  file(GLOB font_parts CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/parts/${font}.part*")
  if(NOT font_parts)
    message(FATAL_ERROR "Missing bundled font parts for ${font}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${font_parts})
  set(combined_font "${SPACEVIEWER_GENERATED_FONTS_DIR}/${font}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E cat ${font_parts}
    OUTPUT_FILE "${combined_font}"
    RESULT_VARIABLE combine_result)
  if(NOT combine_result EQUAL 0)
    message(FATAL_ERROR "Could not assemble bundled font ${font}: ${combine_result}")
  endif()
  list(APPEND SPACEVIEWER_FONTS "${combined_font}")
endforeach()
set(SPACEVIEWER_FONT_LICENSES
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-LICENSE.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/SourceHanSans-LICENSE.txt")

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
  target_link_libraries(spaceviewer PRIVATE comctl32 user32 gdi32 shell32 ole32 uuid oleaut32 comdlg32)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf" "${SPACEVIEWER_GENERATED_FONTS_DIR}/Inter-Regular.ttf" COPYONLY)
  foreach(license IN LISTS SPACEVIEWER_FONT_LICENSES)
    get_filename_component(name "${license}" NAME)
    configure_file("${license}" "${SPACEVIEWER_GENERATED_FONTS_DIR}/${name}" COPYONLY)
  endforeach()
  target_include_directories(spaceviewer PRIVATE "${SPACEVIEWER_GENERATED_FONTS_DIR}" src/platform/win)
  set_property(SOURCE src/platform/win/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${SPACEVIEWER_FONTS};${SPACEVIEWER_FONT_LICENSES}")
elseif(APPLE)
  target_link_libraries(spaceviewer PRIVATE spaceviewer_apple_text)
  target_sources(spaceviewer PRIVATE
    src/platform/apple/window.mm
    src/platform/apple/PrivacyInfo.xcprivacy
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/SpaceViewer.icns"
    ${SPACEVIEWER_FONTS}
    ${SPACEVIEWER_FONT_LICENSES})
  set_source_files_properties(assets/icons/SpaceViewer.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(${SPACEVIEWER_FONTS}
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Fonts)
  set_source_files_properties(${SPACEVIEWER_FONT_LICENSES}
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Fonts)
  set_source_files_properties(src/platform/apple/window.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set_source_files_properties(src/platform/apple/PrivacyInfo.xcprivacy
    PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(spaceviewer PROPERTIES
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED TRUE
    OUTPUT_NAME SpaceViewer
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME SpaceViewer
    MACOSX_BUNDLE_ICON_FILE SpaceViewer.icns
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
  add_custom_command(TARGET spaceviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:spaceviewer>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPACEVIEWER_FONTS} "$<TARGET_FILE_DIR:spaceviewer>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPACEVIEWER_FONT_LICENSES} "$<TARGET_FILE_DIR:spaceviewer>/fonts"
    VERBATIM)
endif()
endif() # SPACEVIEWER_BUILD_APP

if(BUILD_TESTING)
  add_executable(spaceviewer_fill_tests tests/tile_fill_test.cpp)
  target_link_libraries(spaceviewer_fill_tests PRIVATE spaceviewer_core)
  add_test(NAME tile_fill COMMAND spaceviewer_fill_tests)
  add_executable(spaceviewer_layout_tests tests/layout_cache_test.cpp)
  target_link_libraries(spaceviewer_layout_tests PRIVATE spaceviewer_core)
  add_test(NAME layout_cache COMMAND spaceviewer_layout_tests)
  if(APPLE)
    add_executable(spaceviewer_text_tests tests/apple_text_renderer_test.mm)
    set_source_files_properties(tests/apple_text_renderer_test.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(spaceviewer_text_tests PRIVATE spaceviewer_apple_text)
    add_test(NAME apple_text_cache COMMAND spaceviewer_text_tests)
  endif()
  add_executable(spaceviewer_tests tests/main.cpp tests/refactor_test.cpp)
  target_link_libraries(spaceviewer_tests PRIVATE spaceviewer_test_support)
  if(MINGW)
    target_link_options(spaceviewer_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-results/selftest"
    "${CMAKE_CURRENT_BINARY_DIR}/test-results/refactor")
  add_test(NAME selftest COMMAND spaceviewer_tests --self-test)
  set_tests_properties(selftest PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-results/selftest")
  add_test(NAME refactor COMMAND spaceviewer_tests --refactor-test)
  set_tests_properties(refactor PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-results/refactor")
endif()

find_package(Threads REQUIRED)
target_link_libraries(spaceviewer_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
  target_link_libraries(spaceviewer_core PUBLIC stdc++fs)
endif()
