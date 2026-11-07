# src/CMakeLists.txt

add_library(portage_core STATIC
    mem/address_space.cpp
    mem/shared_memory.cpp
    elf/elf_loader.cpp
    elf/link_map.cpp
    rewrite/x86_decode.cpp
    rewrite/syscall_rewriter.cpp
    arch/guest_context.cpp
    syscall/syscall_dispatcher.cpp
    fs/vfs.cpp
    fs/fd_table.cpp
    sync/futex.cpp
    signal/signal_manager.cpp
    thread/thread_manager.cpp
    fs/proc_fs.cpp
    process/process_manager.cpp
    net/socket_manager.cpp
    poll/epoll_manager.cpp
    binder/binder_driver.cpp
    binder/binder_status.cpp
    binder/binder_parcel.cpp
    binder/binder_ndk.cpp
    binder/service_manager.cpp
    vulkan/vulkan_bridge.cpp
    audio/audio_manager.cpp
    audio/aaudio_dispatcher.cpp
    audio/opensles_dispatcher.cpp
    wsi/window_manager.cpp
    input/input_manager.cpp
    input/gamepad_manager.cpp
    input/haptic_manager.cpp
    property/property_manager.cpp
    package/zip_archive.cpp
    package/manifest_parser.cpp
    package/resource_table.cpp
    package/package_manager.cpp
    package/asset_manager.cpp
    package/jni_env.cpp
    package/native_activity.cpp
    ndk/looper.cpp
    ndk/input_queue.cpp
    ndk/configuration.cpp
    ndk/android_log.cpp
    ndk/choreographer.cpp
    ndk/sensor.cpp
    ndk/jnigraphics.cpp
    ndk/hardware_buffer.cpp
    ndk/libandroid_thunks.cpp
    media/media_format.cpp
    media/media_extractor.cpp
    media/media_codec.cpp
    media/wmf_decoder.cpp
    gles/wgl_bridge.cpp
    gles/egl_dispatcher.cpp
    gles/gles_dispatcher.cpp
)

target_compile_features(portage_core PUBLIC cxx_std_20)
target_include_directories(portage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(VULKAN_INCLUDE_DIR)
    target_include_directories(portage_core PUBLIC ${VULKAN_INCLUDE_DIR})
endif()

target_link_libraries(portage_core
    PUBLIC
        synchronization
        ws2_32
        ole32
        user32
        gdi32
        opengl32
        dwmapi
        mfplat
        mfreadwrite
        mfuuid
        ZLIB::ZLIB
    PRIVATE
        project_warnings
)

add_executable(portage launcher/main.cpp)

target_link_libraries(portage
    PRIVATE
        portage_core
        project_warnings
)
