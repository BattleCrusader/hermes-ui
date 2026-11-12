cmake_minimum_required(VERSION 3.21)

project(hermes_ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Release builds get the safe maximum: -O3 (CMake's Release default), LTO, and
# host CPU tuning. Debug/RelWithDebInfo stay debuggable. Turn this off for a
# portable release binary or when profiling with sanitizers.
option(HERMES_MAX_OPTIMIZATION
  "Enable LTO and host CPU tuning for Release builds" ON)
if(HERMES_MAX_OPTIMIZATION)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT HERMES_IPO_SUPPORTED OUTPUT HERMES_IPO_ERROR)
  if(HERMES_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(STATUS "Hermes: LTO unavailable: ${HERMES_IPO_ERROR}")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
      set(HERMES_NATIVE_ARCH_FLAG -mcpu=native)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i.86)$")
      set(HERMES_NATIVE_ARCH_FLAG -march=native)
    endif()
    add_compile_options("$<$<CONFIG:Release>:-O3>")
    add_compile_options("$<$<CONFIG:Release>:-funroll-loops>")
    if(HERMES_NATIVE_ARCH_FLAG)
      add_compile_options(
        "$<$<CONFIG:Release>:${HERMES_NATIVE_ARCH_FLAG}>")
    endif()
  endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg SvgWidgets WebSockets Test Concurrent)
find_package(Qt6 QUIET COMPONENTS Multimedia)

qt_standard_project_setup()

enable_testing()

if(Qt6Multimedia_FOUND)
  add_compile_definitions(HERMES_HAS_MULTIMEDIA)
endif()

add_executable(hermes_ui MACOSX_BUNDLE
  src/main.cc
  src/theme.cc
  src/theme_presets.cc
  src/paint_utils.cc
  src/markdown.cc
  src/message_history.cc
  src/model_visibility.cc
  src/fallback_models.cc
  src/audio_data_url.cc
  src/gateway_endpoint.cc
  src/gateway_http.cc
  src/gateway_socket.cc
  src/interrupt_relay.cc
  src/background_task.cc
  src/qr_code.cc
  src/artifacts.cc
  src/browser_automation.cc
  third_party/qrcodegen/qrcodegen.cpp
  src/gateway_replay.cc
  src/gateway_protocol.cc
  src/prompt_submit.cc
  src/profile_worker.cc
  src/session_usage.cc
  src/session_transfer.cc
  src/subagents.cc
  src/todo_state.cc
  src/quick_entry.cc
  src/pet_sprite.cc
  src/pet_overlay.cc
  src/profile_selection.cc
  src/projects.cc
  src/project_dialog.cc
  src/project_appearance.cc
  src/prompt_history_dialog.cc
  src/cache_store.cc
  src/right_rail.cc
  src/session_search_bar.cc
  src/terminal_widget.cc
  src/gateway_events.cc
  src/supervisor.cc
  src/title_bar.cc
  src/sidebar.cc
  src/chat_view.cc
  src/request_bar.cc
  src/command_palette.cc
  src/preferences_dialog.cc
  src/settings_view.cc
  src/fallback_models.cc
  src/settings_pages.cc
  src/pages_view.cc
  src/native_prefs.cc
  src/notification_prefs.cc
  src/notification_service.cc
  src/widget_theme.cc
  src/composer.cc
  src/detached_chat.cc
  src/slash_commands.cc
  src/status_bar.cc
  src/main_window.cc
  resources/resources.qrc
)

if(APPLE)
  set_target_properties(hermes_ui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Hermes"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nousresearch.hermes.ui"
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )
endif()

target_include_directories(hermes_ui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_ui PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::SvgWidgets
  Qt6::WebSockets
  Qt6::Concurrent
  $<$<BOOL:${Qt6Multimedia_FOUND}>:Qt6::Multimedia>
)

target_compile_options(hermes_ui PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(hermes_ui PRIVATE src/macos_notifications.mm)
  target_link_libraries(hermes_ui PRIVATE
    "-framework AppKit"
    "-framework Foundation"
    "-framework UserNotifications"
  )
  target_compile_options(hermes_ui PRIVATE -Wno-deprecated-declarations)

  # Keep the familiar ./build/hermes_ui entry point, but run the executable
  # inside the .app. NSUserNotificationCenter is nil for an unbundled
  # executable, and Notification Center rejects an app it cannot resolve as a
  # bundle. A symlink is not enough: NSBundle does not resolve it.
  add_custom_command(TARGET hermes_ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/hermes_ui_launcher.sh"
      "${CMAKE_BINARY_DIR}/hermes_ui"
    COMMAND chmod +x "${CMAKE_BINARY_DIR}/hermes_ui"
    VERBATIM)
endif()


add_executable(hermes_ui_tests
  tests/preferences_test.cc
  src/theme.cc
  src/theme_presets.cc
  src/paint_utils.cc
  src/preferences_dialog.cc
  src/composer.cc
  src/model_visibility.cc
  src/fallback_models.cc
  src/session_usage.cc
  src/message_history.cc
  src/session_transfer.cc
  src/subagents.cc
  src/todo_state.cc
  src/quick_entry.cc
  src/artifacts.cc
  src/right_rail.cc
  src/session_search_bar.cc
  src/terminal_widget.cc
  src/profile_selection.cc
  src/projects.cc
  src/project_dialog.cc
  src/project_appearance.cc
  src/prompt_history_dialog.cc
  src/cache_store.cc
  src/browser_automation.cc
  src/gateway_events.cc
  src/audio_data_url.cc
  src/notification_prefs.cc
  src/native_prefs.cc
  src/slash_commands.cc
  src/prompt_submit.cc
  src/status_bar.cc
  resources/resources.qrc
)

target_include_directories(hermes_ui_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_ui_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::SvgWidgets
  Qt6::WebSockets
  Qt6::Test
)

target_compile_options(hermes_ui_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_executable(hermes_pet_tests
  tests/pet_overlay_test.cc
  src/pet_sprite.cc
  src/pet_overlay.cc
  src/theme.cc
  src/theme_presets.cc
  src/native_prefs.cc
  resources/resources.qrc
)

target_include_directories(hermes_pet_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_pet_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
)

target_compile_options(hermes_pet_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME native_ui COMMAND hermes_ui_tests)

add_executable(hermes_settings_tests
  tests/settings_view_test.cc
  src/settings_view.cc
  src/fallback_models.cc
  src/settings_pages.cc
  src/qr_code.cc
  third_party/qrcodegen/qrcodegen.cpp
  src/subagents.cc
  src/todo_state.cc
  src/quick_entry.cc
  src/artifacts.cc
  src/right_rail.cc
  src/terminal_widget.cc
  src/gateway_events.cc
  src/native_prefs.cc
  src/notification_prefs.cc
  src/theme.cc
  src/theme_presets.cc
  resources/resources.qrc
)

target_include_directories(hermes_settings_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_settings_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::SvgWidgets
  Qt6::Test
  $<$<BOOL:${Qt6Multimedia_FOUND}>:Qt6::Multimedia>
)

target_compile_options(hermes_settings_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME native_settings COMMAND hermes_settings_tests)

add_executable(hermes_terminal_tests
  tests/terminal_widget_test.cc
  src/terminal_widget.cc
  src/theme.cc
  src/theme_presets.cc
  resources/resources.qrc
)

target_include_directories(hermes_terminal_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_terminal_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)

target_compile_options(hermes_terminal_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME native_terminal COMMAND hermes_terminal_tests)

add_executable(hermes_sidebar_tests
  tests/sidebar_test.cc
  src/sidebar.cc
  src/projects.cc
  src/project_appearance.cc
  src/theme.cc
  src/theme_presets.cc
  src/paint_utils.cc
  resources/resources.qrc
)

target_include_directories(hermes_sidebar_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_sidebar_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)

add_test(NAME native_sidebar COMMAND hermes_sidebar_tests)

add_executable(hermes_chat_tests
  tests/chat_view_test.cc
  src/chat_view.cc
  src/detached_chat.cc
  src/composer.cc
  src/model_visibility.cc
  src/native_prefs.cc
  src/slash_commands.cc
  src/request_bar.cc
  src/markdown.cc
  src/prompt_submit.cc
  src/theme.cc
  src/theme_presets.cc
  src/paint_utils.cc
  resources/resources.qrc
)

target_include_directories(hermes_chat_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_chat_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)

add_test(NAME native_chat COMMAND hermes_chat_tests)

if(APPLE)
  add_test(NAME macos_bundle_identity
    COMMAND ${CMAKE_COMMAND}
      "-DAPP_BUNDLE=$<TARGET_BUNDLE_DIR:hermes_ui>"
      "-DLAUNCHER=${CMAKE_BINARY_DIR}/hermes_ui"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_macos_bundle.cmake")
endif()

add_executable(hermes_click_tests
  tests/click_audit_test.cc
  src/chat_view.cc
  src/request_bar.cc
  src/markdown.cc
  src/model_visibility.cc
  src/native_prefs.cc
  src/composer.cc
  src/slash_commands.cc
  src/sidebar.cc
  src/projects.cc
  src/project_appearance.cc
  src/title_bar.cc
  src/command_palette.cc
  src/theme.cc
  src/theme_presets.cc
  src/paint_utils.cc
  resources/resources.qrc
)

target_include_directories(hermes_click_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_click_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)

add_test(NAME native_click_audit COMMAND hermes_click_tests)

add_executable(hermes_gateway_tests
  tests/gateway_config_test.cc
  src/gateway_http.cc
  src/gateway_endpoint.cc
)

add_executable(hermes_protocol_tests
  tests/gateway_protocol_test.cc
  src/gateway_replay.cc
  src/gateway_events.cc
  src/gateway_protocol.cc
)

target_include_directories(hermes_protocol_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_protocol_tests PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)

target_compile_options(hermes_protocol_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME gateway_protocol COMMAND hermes_protocol_tests)

target_include_directories(hermes_gateway_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermes_gateway_tests PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Test
)

target_compile_options(hermes_gateway_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME gateway_config COMMAND hermes_gateway_tests)
