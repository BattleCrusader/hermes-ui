# Verifies that the macOS build produces a real app bundle. Notification Center
# rejects an unbundled executable even when it embeds an Info.plist section, so
# this packaging invariant keeps native notifications working.

if(NOT DEFINED APP_BUNDLE)
  message(FATAL_ERROR "APP_BUNDLE was not provided")
endif()

set(plist "${APP_BUNDLE}/Contents/Info.plist")
if(NOT EXISTS "${plist}")
  message(FATAL_ERROR "missing bundle Info.plist: ${plist}")
endif()

execute_process(
  COMMAND /usr/bin/plutil -extract CFBundleIdentifier raw -o - "${plist}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE bundle_id
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "could not read CFBundleIdentifier from ${plist}")
endif()
if(NOT bundle_id STREQUAL "com.nousresearch.hermes.ui")
  message(FATAL_ERROR "unexpected CFBundleIdentifier: ${bundle_id}")
endif()

execute_process(
  COMMAND /usr/bin/plutil -extract CFBundleExecutable raw -o - "${plist}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE executable
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "could not read CFBundleExecutable from ${plist}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Contents/MacOS/${executable}")
  message(FATAL_ERROR
    "bundle executable is missing: ${APP_BUNDLE}/Contents/MacOS/${executable}")
endif()

if(NOT EXISTS "${LAUNCHER}")
  message(FATAL_ERROR "launcher is missing: ${LAUNCHER}")
endif()
