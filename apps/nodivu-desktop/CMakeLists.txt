cmake_minimum_required(VERSION 3.25)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NODIVU_APP_VERSION LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" NODIVU_NUMERIC_VERSION "${NODIVU_APP_VERSION}")
project(NodivuDesktop VERSION ${NODIVU_NUMERIC_VERSION} LANGUAGES C CXX)

# MSVC's pt-BR diagnostics use UTF-8 with /utf-8. CMake 3.30's compiler
# probe decodes that prefix as OEM text, so Ninja otherwise misses headers.
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^Observa.*incluindo arquivo")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Observação: incluindo arquivo: ")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The GUI lives at the package root; Windows resolves Qt DLLs beside that EXE.
set(CMAKE_INSTALL_BINDIR ".")
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
add_library(nodivu_crypto STATIC
    ../../third_party/monocypher-4.0.3/monocypher.c
    ../../third_party/monocypher-4.0.3/monocypher-ed25519.c)
target_include_directories(nodivu_crypto SYSTEM PUBLIC ../../third_party/monocypher-4.0.3)
add_library(nodivu_zip STATIC ../../third_party/miniz-3.1.2/miniz.c)
target_include_directories(nodivu_zip SYSTEM PUBLIC ../../third_party/miniz-3.1.2)
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

add_library(nodivu_frontend_core STATIC
    src/platform/install_transaction.cpp
    src/platform/install_transaction.hpp
    src/platform/profile_backup.cpp
    src/platform/profile_backup.hpp
    src/platform/single_instance.cpp
    src/platform/single_instance.hpp
    src/platform/diagnostic_log.cpp
    src/platform/diagnostic_log.hpp
    src/security/checked_json.cpp
    src/security/checked_json.hpp
    src/security/tuf_metadata.cpp
    src/security/tuf_metadata.hpp
    src/contracts/protocol_codec.cpp
    src/contracts/protocol_codec.hpp
    src/backend/jsonl_transport.cpp
    src/backend/jsonl_transport.hpp
    src/backend/backend_client.cpp
    src/backend/backend_client.hpp
    src/platform/app_paths.cpp
    src/platform/app_paths.hpp
)
target_include_directories(nodivu_frontend_core PUBLIC src)
target_link_libraries(nodivu_frontend_core PUBLIC Qt6::Core Qt6::Network nodivu_crypto)
target_compile_definitions(nodivu_frontend_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

add_library(nodivu_frontend_ui STATIC
    src/ui/dialogs/pending_changes_dialog.cpp
    src/ui/dialogs/pending_changes_dialog.hpp
    src/features/graph/routing_panel.cpp
    src/features/graph/routing_panel.hpp
    src/contracts/playback_state.cpp
    src/contracts/playback_state.hpp
    src/contracts/monitor_state.cpp
    src/contracts/monitor_state.hpp
    src/contracts/signal_state.cpp
    src/contracts/signal_state.hpp
    src/contracts/audio_state.cpp
    src/contracts/audio_state.hpp
    src/contracts/virtual_cable.cpp
    src/contracts/virtual_cable.hpp
    src/features/workspace/audio_status_panel.cpp
    src/features/workspace/audio_status_panel.hpp
    src/features/workspace/system_panel.cpp
    src/features/workspace/system_panel.hpp
    src/features/updates/update_panel.cpp
    src/features/updates/update_panel.hpp
    src/features/updates/update_service.cpp
    src/features/updates/update_service.hpp
    src/features/updates/update_window.cpp
    src/features/updates/update_window.hpp
    src/features/store/store_service.cpp
    src/features/store/store_service.hpp
    src/features/store/store_controller.cpp
    src/features/store/store_controller.hpp
    src/features/store/readme_view.cpp
    src/features/store/readme_view.hpp
    src/features/store/store_window.cpp
    src/features/store/store_window.hpp
    src/features/store/network_transfer.cpp
    src/features/store/network_transfer.hpp
    src/features/store/tuf_client.cpp
    src/features/store/tuf_client.hpp
    src/features/plugins/package_service.cpp
    src/features/plugins/package_service.hpp
    src/features/projects/project_repository.cpp
    src/features/projects/project_repository.hpp
    src/features/projects/project_controller.cpp
    src/features/projects/project_controller.hpp
    src/contracts/node_descriptor.cpp
    src/contracts/node_descriptor.hpp
    src/features/workspace/session_controller.cpp
    src/features/workspace/session_controller.hpp
    src/features/graph/graph_scene.cpp
    src/features/graph/graph_scene.hpp
    src/features/graph/node_content.cpp
    src/features/graph/node_content.hpp
    src/features/graph/capture_source_widget.cpp
    src/features/graph/capture_source_widget.hpp
    src/ui/controls/device_selector.cpp
    src/ui/controls/device_selector.hpp
    src/ui/controls/inline_check_box.cpp
    src/ui/controls/inline_check_box.hpp
    src/ui/controls/info_message.cpp
    src/ui/controls/info_message.hpp
    src/ui/controls/control_presentation.cpp
    src/ui/controls/control_presentation.hpp
    src/ui/controls/worker_controls.cpp
    src/ui/controls/worker_controls.hpp
    src/ui/controls/action_availability.cpp
    src/ui/controls/action_availability.hpp
    src/ui/theme/studio_panel_style.cpp
    src/ui/theme/studio_panel_style.hpp
    src/ui/controls/player_details.cpp
    src/ui/controls/player_details.hpp
    src/features/graph/graph_view.cpp
    src/features/graph/graph_view.hpp
    src/features/library/library_panel.cpp
    src/features/library/library_panel.hpp
    src/features/library/library_block_button.cpp
    src/features/library/library_block_button.hpp
    src/features/inspector/inspector_panel.cpp
    src/features/inspector/inspector_panel.hpp
    src/ui/theme/studio_icons.cpp
    src/ui/theme/studio_icons.hpp
    src/ui/theme/theme.cpp
    src/ui/theme/theme.hpp
    src/features/workspace/main_window.cpp
    src/features/workspace/main_window.hpp
    src/features/graph/empty_graph_widget.cpp
    src/features/graph/empty_graph_widget.hpp
)
target_include_directories(nodivu_frontend_ui PUBLIC src)
target_link_libraries(nodivu_frontend_ui PUBLIC nodivu_frontend_core Qt6::Widgets Qt6::Network Qt6::Concurrent nodivu_zip)

qt_add_executable(nodivu WIN32 src/app/main.cpp src/app/smoke_scenario.cpp src/app/smoke_scenario.hpp)
target_link_libraries(nodivu PRIVATE nodivu_frontend_ui)
target_compile_definitions(nodivu PRIVATE NODIVU_APP_VERSION="${NODIVU_APP_VERSION}")
add_executable(nodivu-diagnostics src/app/diagnostics.cpp)
target_link_libraries(nodivu-diagnostics PRIVATE nodivu_frontend_ui)
add_executable(nodivu-install-tool src/app/install_tool.cpp)
target_link_libraries(nodivu-install-tool PRIVATE nodivu_frontend_core)
install(TARGETS nodivu-install-tool RUNTIME DESTINATION .)
set_target_properties(nodivu PROPERTIES OUTPUT_NAME nodivu)
if(WIN32)
    enable_language(RC)
    target_sources(nodivu_frontend_ui PRIVATE src/platform/windows/confirmation_dialog.cpp src/platform/windows/confirmation_dialog.hpp)
    target_link_libraries(nodivu_frontend_ui PRIVATE comctl32)
    if(MSVC)
        target_link_options(nodivu_frontend_ui INTERFACE "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
    endif()
    target_sources(nodivu_frontend_core PRIVATE src/platform/windows/native_desktop.cpp src/platform/windows/native_desktop.hpp)
    target_link_libraries(nodivu_frontend_core PRIVATE shell32 dwmapi advapi32)
    add_executable(nodivu-icon-tool src/app/icon_tool.cpp)
    target_include_directories(nodivu-icon-tool PRIVATE src)
    target_link_libraries(nodivu-icon-tool PRIVATE Qt6::Gui)
    target_compile_options(nodivu-icon-tool PRIVATE /W4 /WX /utf-8 /permissive-)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/nodivu.ico"
        COMMAND nodivu-icon-tool "${CMAKE_CURRENT_BINARY_DIR}/nodivu.ico"
        DEPENDS nodivu-icon-tool VERBATIM)
    add_custom_target(nodivu-icon DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/nodivu.ico")
    add_dependencies(nodivu nodivu-icon)
    configure_file(resources/windows/application.rc.in application.rc @ONLY)
    target_sources(nodivu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/application.rc")
    add_executable(nodivu-update-helper WIN32 src/platform/windows/update_helper.cpp)
    set_target_properties(nodivu-update-helper PROPERTIES AUTOMOC OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(nodivu-update-helper PRIVATE bcrypt shell32)
    target_compile_options(nodivu-update-helper PRIVATE /W4 /WX /utf-8 /permissive-)
    target_compile_definitions(nodivu-update-helper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    install(TARGETS nodivu-update-helper RUNTIME DESTINATION .)
endif()

foreach(target nodivu_frontend_core nodivu_frontend_ui nodivu nodivu-diagnostics nodivu-install-tool)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /EHsc)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

if(BUILD_TESTING)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

install(TARGETS nodivu RUNTIME DESTINATION .)
install(FILES resources/store/source.json DESTINATION resources/store)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/updates/source.json")
    install(FILES resources/updates/source.json DESTINATION resources/updates)
endif()
qt_generate_deploy_app_script(TARGET nodivu OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR
    NO_TRANSLATIONS DEPLOY_TOOL_OPTIONS --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
    --skip-plugin-types imageformats)
if(WIN32)
    foreach(image_plugin qjpeg qgif qwebp)
        install(FILES "${Qt6_DIR}/../../../plugins/imageformats/${image_plugin}.dll" DESTINATION plugins/imageformats)
    endforeach()
endif()
install(SCRIPT ${deploy_script})
