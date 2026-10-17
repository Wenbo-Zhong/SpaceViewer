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

if(WIN32)
  target_sources(spaceviewer PRIVATE src/platform_win.cpp src/app.rc)
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
      COMMAND "${CMAKE_C_COMPILER}" -c "${CMAKE_CURRENT_SOURCE_DIR}/src/empty_manifest.c" -o "${manifest_dir}/default-manifest.o"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/empty_manifest.c"
      VERBATIM)
    add_custom_target(spaceviewer_manifest DEPENDS "${manifest_dir}/default-manifest.o")
    add_dependencies(spaceviewer spaceviewer_manifest)
    target_link_options(spaceviewer PRIVATE -static -static-libgcc -static-libstdc++ "SHELL:-B ${manifest_dir}/" -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
  endif()
  target_link_libraries(spaceviewer PRIVATE user32 gdi32 shell32 ole32 uuid comdlg32)
elseif(APPLE)
  target_sources(spaceviewer PRIVATE src/platform_mac.mm)
  set_source_files_properties(src/platform_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set_target_properties(spaceviewer PROPERTIES OUTPUT_NAME SpaceViewer MACOSX_BUNDLE FALSE)
  target_link_libraries(spaceviewer PRIVATE "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics" "-framework CoreServices")
else()
  target_sources(spaceviewer PRIVATE src/platform_x11.cpp)
  find_package(X11 REQUIRED)
  find_path(XFT_INCLUDE_DIR X11/Xft/Xft.h)
  find_library(XFT_LIBRARY Xft)
  if(NOT XFT_INCLUDE_DIR OR NOT XFT_LIBRARY)
    message(FATAL_ERROR "libXft development files are required (Ubuntu: libxft-dev)")
  endif()
  target_include_directories(spaceviewer PRIVATE ${X11_INCLUDE_DIR} ${XFT_INCLUDE_DIR})
  target_link_libraries(spaceviewer PRIVATE ${X11_LIBRARIES} ${XFT_LIBRARY})
  set_target_properties(spaceviewer PROPERTIES OUTPUT_NAME spaceviewer)
endif()

find_package(Threads REQUIRED)
target_link_libraries(spaceviewer PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
  target_link_libraries(spaceviewer PRIVATE stdc++fs)
endif()
