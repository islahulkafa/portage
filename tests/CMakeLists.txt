# tests/CMakeLists.txt

add_subdirectory(guests)

include(GoogleTest)

set(UNIT_TEST_SOURCES
    unit/test_x86_decode.cpp
    unit/test_elf_loader.cpp
    unit/test_syscall_rewriter.cpp
    unit/test_vfs.cpp
    unit/test_futex.cpp
    unit/test_fd_table.cpp
    unit/test_thread_manager.cpp
    unit/test_signal_manager.cpp
    unit/test_proc_fs.cpp
    unit/test_dirent.cpp
    unit/test_pipe.cpp
    unit/test_socket_manager.cpp
    unit/test_epoll_manager.cpp
    unit/test_binder_driver.cpp
    unit/test_vulkan_bridge.cpp
    unit/test_audio_manager.cpp
    unit/test_window_manager.cpp
    unit/test_input_manager.cpp
    unit/test_property_manager.cpp
    unit/test_zip_archive.cpp
    unit/test_manifest_parser.cpp
    unit/test_package_manager.cpp
    unit/test_resource_table.cpp
    unit/test_asset_manager.cpp
    unit/test_jni_env.cpp
    unit/test_looper.cpp
    unit/test_choreographer.cpp
    unit/test_sensor.cpp
    unit/test_surface_rendering.cpp
    unit/test_ashmem.cpp
    unit/test_hardware_buffer.cpp
    unit/test_input_queue.cpp
    unit/test_configuration.cpp
    unit/test_android_log.cpp
    unit/test_egl.cpp
    unit/test_gles.cpp
    unit/test_key_codes.cpp
    unit/test_aaudio.cpp
    unit/test_opensles.cpp
    unit/test_link_map.cpp
    unit/test_mediandk.cpp
    unit/test_binder_ndk.cpp
    unit/test_input_gamepad.cpp
    unit/test_storage_manager.cpp
    unit/test_play_services.cpp
    unit/test_multinetwork.cpp
    unit/test_adpf.cpp
    unit/test_audio_recording.cpp
    unit/test_swappy.cpp
    unit/test_camera.cpp
    unit/test_keystore.cpp
    unit/test_location.cpp
    unit/test_clipboard.cpp
    unit/test_power.cpp
    unit/test_notification.cpp
)

set(INTEGRATION_TEST_SOURCES
    integration/test_static_execution.cpp
    integration/test_dynamic_execution.cpp
    integration/test_threads_execution.cpp
    integration/test_fs_execution.cpp
    integration/test_net_execution.cpp
    integration/test_binder_execution.cpp
    integration/test_vulkan_execution.cpp
    integration/test_audio_execution.cpp
    integration/test_presentation_execution.cpp
    integration/test_input_execution.cpp
    integration/test_property_execution.cpp
    integration/test_game_engine_execution.cpp
    integration/test_gles_execution.cpp
)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests
    PRIVATE
        portage_core
        project_warnings
        GTest::gtest
        GTest::gtest_main
        ZLIB::ZLIB
)
target_include_directories(unit_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests
    PRIVATE
        portage_core
        project_warnings
        GTest::gtest
        GTest::gtest_main
        ZLIB::ZLIB
)
target_include_directories(integration_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_dependencies(integration_tests build_guest_binaries)

gtest_discover_tests(unit_tests
    PROPERTIES
        LABELS "unit"
        DISCOVERY_TIMEOUT 15
)

gtest_discover_tests(integration_tests
    PROPERTIES
        LABELS "integration"
        DISCOVERY_TIMEOUT 30
)
