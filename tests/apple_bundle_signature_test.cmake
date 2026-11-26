# TCC stores the designated requirement when granting access. Both slices of a
# universal app must satisfy that requirement, and the bundle must be sealed.
if(NOT IS_DIRECTORY "${SPACEVIEWER_APP_BUNDLE}")
  message(FATAL_ERROR "Missing application bundle: ${SPACEVIEWER_APP_BUNDLE}")
endif()

execute_process(
  COMMAND /usr/bin/codesign --verify --strict --all-architectures "${SPACEVIEWER_APP_BUNDLE}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Invalid application signature: ${output}${error}")
endif()

execute_process(
  COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier"
    "${SPACEVIEWER_APP_BUNDLE}/Contents/Info.plist"
  RESULT_VARIABLE status OUTPUT_VARIABLE bundle_identifier ERROR_VARIABLE error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status EQUAL 0 OR NOT bundle_identifier STREQUAL "com.spaceviewer.app")
  message(FATAL_ERROR "Unexpected bundle identifier: ${bundle_identifier}${error}")
endif()

execute_process(
  COMMAND /usr/bin/codesign --display --verbose=4 --requirements - "${SPACEVIEWER_APP_BUNDLE}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(signature "${output}${error}")
if(NOT status EQUAL 0 OR NOT signature MATCHES "Identifier=${bundle_identifier}\n")
  message(FATAL_ERROR "Signature identifier does not match Info.plist: ${signature}")
endif()
if(NOT signature MATCHES "designated => ([^\n]+)")
  message(FATAL_ERROR "Missing designated requirement: ${signature}")
endif()
set(requirement "${CMAKE_MATCH_1}")

execute_process(
  COMMAND /usr/bin/codesign --verify --strict --all-architectures
    "-R=${requirement}" "${SPACEVIEWER_APP_BUNDLE}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Application fails its own designated requirement: ${output}${error}")
endif()
message(STATUS "Bundle signature and designated requirement are valid for all architectures")
