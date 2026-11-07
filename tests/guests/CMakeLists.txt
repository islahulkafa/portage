# tests/guests/CMakeLists.txt

set(GUEST_BINARIES
    hello_freestanding
    test_tls_canary
    test_memory
    test_auxv
    test_threads
    test_signal_recovery
    test_pthread
    test_fs_dirent
    test_proc_reading
    test_ipc_pipe
    toybox_mini
    test_net_loopback
    test_epoll_echo
    test_time_nanosleep
    test_binder_open
    test_audio_pcm
    test_input_evdev
    test_property_bionic
)

foreach(guest ${GUEST_BINARIES})
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${guest}.elf"
        COMMAND "${CMAKE_C_COMPILER}"
            --target=x86_64-linux-android
            -fuse-ld=lld
            -nostdlib
            -fPIE
            "-Wl,-pie"
            "-Wl,--no-dynamic-linker"
            -O2
            "${CMAKE_CURRENT_SOURCE_DIR}/${guest}.c"
            -o "${CMAKE_CURRENT_BINARY_DIR}/${guest}.elf"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${guest}.c"
        COMMENT "Cross-compiling Android x86_64 static PIE guest ELF: ${guest}.elf"
    )
    add_custom_target(build_guest_${guest} DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${guest}.elf")
endforeach()

# Shared library test_lib.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/test_lib.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_lib.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/test_lib.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_lib.c"
    COMMENT "Cross-compiling Android x86_64 shared library: test_lib.so"
)

# Shared library libvulkan.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libvulkan.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -I "${VULKAN_INCLUDE_DIR}"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libvulkan.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
    COMMENT "Cross-compiling Android x86_64 Vulkan shared library: libvulkan.so"
)

# Shared library libandroid.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libandroid.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libandroid.c"
    COMMENT "Cross-compiling Android x86_64 native window library: libandroid.so"
)

# Shared library libtest_game_engine.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -fno-builtin
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_game_engine.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_game_engine.c"
    COMMENT "Cross-compiling Android x86_64 Game Engine library: libtest_game_engine.so"
)

# Shared library libEGL.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libEGL.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libEGL.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libEGL.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libEGL.c"
    COMMENT "Cross-compiling Android x86_64 EGL shared library: libEGL.so"
)

# Shared library libGLESv2.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libGLESv2.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libGLESv2.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libGLESv2.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libGLESv2.c"
    COMMENT "Cross-compiling Android x86_64 OpenGL ES shared library: libGLESv2.so"
)

# Shared library libtest_gles_quad.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libtest_gles_quad.so"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -fno-builtin
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gles_quad.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/libtest_gles_quad.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_gles_quad.c"
    COMMENT "Cross-compiling Android x86_64 GLES quad test library: libtest_gles_quad.so"
)

# Vulkan device test executable
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_device.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -I "${VULKAN_INCLUDE_DIR}"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_device.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_device.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_device.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
    COMMENT "Cross-compiling Android x86_64 Vulkan device test ELF: test_vulkan_device.elf"
)
add_custom_target(build_guest_test_vulkan_device DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_device.elf")

# Vulkan presentation test executable
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_presentation.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -I "${VULKAN_INCLUDE_DIR}"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_presentation.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libandroid.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_presentation.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_presentation.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libandroid.c"
    COMMENT "Cross-compiling Android x86_64 Vulkan presentation test ELF: test_vulkan_presentation.elf"
)
add_custom_target(build_guest_test_vulkan_presentation DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_presentation.elf")

# Test dynamic linker / interpreter: rootfs/system/bin/linker64
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/bin/linker64"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/bin"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        "-Wl,-e,interp_entry"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_interp.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/bin/linker64"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_interp.c"
    COMMENT "Cross-compiling Android x86_64 dynamic linker: linker64"
)

# Dynamic executable requesting /system/bin/linker64
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hello_dynamic.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,-dynamic-linker,/system/bin/linker64"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/hello_dynamic.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/hello_dynamic.elf"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hello_dynamic.c"
    COMMENT "Cross-compiling Android x86_64 dynamic ELF: hello_dynamic.elf"
)

add_custom_target(build_guest_binaries
    DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/hello_freestanding.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_tls_canary.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_memory.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_auxv.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/test_lib.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/bin/linker64"
        "${CMAKE_CURRENT_BINARY_DIR}/hello_dynamic.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_threads.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_signal_recovery.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_pthread.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_fs_dirent.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_proc_reading.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_ipc_pipe.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/toybox_mini.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_net_loopback.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_epoll_echo.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_time_nanosleep.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_binder_open.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libvulkan.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libEGL.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libGLESv2.so"
        "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_device.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_presentation.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_audio_pcm.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_input_evdev.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_property_bionic.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_gles_quad.so"
)
