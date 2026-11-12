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

# Shared library libnativewindow.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnativewindow.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libnativewindow.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnativewindow.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libnativewindow.c"
    COMMENT "Cross-compiling Android x86_64 native window library: libnativewindow.so"
)

# Shared library libgame_math.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_game_math.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_game_math.c"
    COMMENT "Cross-compiling Android x86_64 Game Math library: libgame_math.so"
)

# Shared library libgame_plugin.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libgame_plugin.so"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_game_plugin.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/libgame_plugin.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_game_plugin.c"
    COMMENT "Cross-compiling Android x86_64 Game Plugin library: libgame_plugin.so"
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
        -L "${CMAKE_CURRENT_BINARY_DIR}"
        -lgame_math
        -o "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_game_engine.c"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
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

# Shared library libaaudio.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libaaudio.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "-Wl,-soname,libaaudio.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libaaudio.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libaaudio.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libaaudio.c"
    COMMENT "Cross-compiling Android x86_64 AAudio shared library: libaaudio.so"
)

# Shared library libOpenSLES.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libOpenSLES.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "-Wl,-soname,libOpenSLES.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libOpenSLES.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libOpenSLES.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libOpenSLES.c"
    COMMENT "Cross-compiling Android x86_64 OpenSL ES shared library: libOpenSLES.so"
)

# Shared library libdl.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libdl.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libdl.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libdl.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libdl.c"
    COMMENT "Cross-compiling Android x86_64 libdl shared library: libdl.so"
)

# Shared library libjnigraphics.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libjnigraphics.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libjnigraphics.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libjnigraphics.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libjnigraphics.c"
    COMMENT "Cross-compiling Android x86_64 libjnigraphics shared library: libjnigraphics.so"
)

# Shared library libmediandk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libmediandk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libmediandk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libmediandk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libmediandk.c"
    COMMENT "Cross-compiling Android x86_64 libmediandk shared library: libmediandk.so"
)

# Shared library libbinder_ndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libbinder_ndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libbinder_ndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libbinder_ndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libbinder_ndk.c"
    COMMENT "Cross-compiling Android x86_64 libbinder_ndk shared library: libbinder_ndk.so"
)

# Shared library libswappy_vk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_vk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libswappy_vk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_vk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libswappy_vk.c"
    COMMENT "Cross-compiling Android x86_64 libswappy_vk shared library: libswappy_vk.so"
)

# Shared library libswappy_gl.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_gl.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libswappy_gl.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_gl.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libswappy_gl.c"
    COMMENT "Cross-compiling Android x86_64 libswappy_gl shared library: libswappy_gl.so"
)

# Shared library libcamera2ndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libcamera2ndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libcamera2ndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libcamera2ndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libcamera2ndk.c"
    COMMENT "Cross-compiling Android x86_64 libcamera2ndk shared library: libcamera2ndk.so"
)

# Shared library libkeystorendk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libkeystorendk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libkeystorendk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libkeystorendk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libkeystorendk.c"
    COMMENT "Cross-compiling Android x86_64 libkeystorendk shared library: libkeystorendk.so"
)

# Shared library liblocationndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/liblocationndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/liblocationndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/liblocationndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/liblocationndk.c"
    COMMENT "Cross-compiling Android x86_64 liblocationndk shared library: liblocationndk.so"
)

# Shared library libclipboardndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libclipboardndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libclipboardndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libclipboardndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libclipboardndk.c"
    COMMENT "Cross-compiling Android x86_64 libclipboardndk shared library: libclipboardndk.so"
)

# Shared library libpowerndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libpowerndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libpowerndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libpowerndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libpowerndk.c"
    COMMENT "Cross-compiling Android x86_64 libpowerndk shared library: libpowerndk.so"
)

# Shared library libnotificationndk.so
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnotificationndk.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/libnotificationndk.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnotificationndk.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libnotificationndk.c"
    COMMENT "Cross-compiling Android x86_64 libnotificationndk shared library: libnotificationndk.so"
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

# Vulkan compute pipeline test executable
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_compute_guest.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -I "${VULKAN_INCLUDE_DIR}"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_compute_guest.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_compute_guest.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_vulkan_compute_guest.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libvulkan.c"
    COMMENT "Cross-compiling Android x86_64 Vulkan compute test ELF: test_vulkan_compute_guest.elf"
)
add_custom_target(build_guest_test_vulkan_compute_guest DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_compute_guest.elf")

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

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "-Wl,-soname,libc.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libc.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libc.c"
    COMMENT "Cross-compiling Android x86_64 shared library: libc.so"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_bionic_extensions_guest.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_bionic_extensions_guest.c"
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        -lc
        -ldl
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_bionic_extensions_guest.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_bionic_extensions_guest.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libdl.so"
    COMMENT "Cross-compiling Android x86_64 Bionic extensions test ELF: test_bionic_extensions_guest.elf"
)
add_custom_target(build_guest_test_bionic_extensions_guest DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_bionic_extensions_guest.elf")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_cpp_runtime_guest.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_cpp_runtime_guest.c"
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        -lc
        -ldl
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_cpp_runtime_guest.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_cpp_runtime_guest.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libdl.so"
    COMMENT "Cross-compiling Android x86_64 C++ runtime and Bionic stdio test ELF: test_cpp_runtime_guest.elf"
)
add_custom_target(build_guest_test_cpp_runtime_guest DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_cpp_runtime_guest.elf")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_aaudio_lifecycle.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fno-stack-protector
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_aaudio_lifecycle.c"
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        -laaudio
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_aaudio_lifecycle.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_aaudio_lifecycle.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libaaudio.so"
    COMMENT "Cross-compiling Android x86_64 AAudio lifecycle test ELF: test_aaudio_lifecycle.elf"
)
add_custom_target(build_guest_test_aaudio_lifecycle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_aaudio_lifecycle.elf")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_mediandk_pipeline.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fno-stack-protector
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_mediandk_pipeline.c"
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        -lmediandk
        -landroid
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_mediandk_pipeline.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_mediandk_pipeline.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libmediandk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
    COMMENT "Cross-compiling Android x86_64 Media NDK pipeline test ELF: test_mediandk_pipeline.elf"
)
add_custom_target(build_guest_test_mediandk_pipeline DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_mediandk_pipeline.elf")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_camera2_pipeline.elf"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fno-stack-protector
        -fPIE
        "-Wl,-pie"
        "-Wl,--no-dynamic-linker"
        -O2
        "${CMAKE_CURRENT_SOURCE_DIR}/test_camera2_pipeline.c"
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        -lcamera2ndk
        -lmediandk
        -landroid
        -o "${CMAKE_CURRENT_BINARY_DIR}/test_camera2_pipeline.elf"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_camera2_pipeline.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libcamera2ndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libmediandk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
    COMMENT "Cross-compiling Android x86_64 Camera2 NDK pipeline test ELF: test_camera2_pipeline.elf"
)
add_custom_target(build_guest_test_camera2_pipeline DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_camera2_pipeline.elf")

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
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnativewindow.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libEGL.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libGLESv2.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libaaudio.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libOpenSLES.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libdl.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libjnigraphics.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libmediandk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libbinder_ndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_vk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libswappy_gl.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libcamera2ndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libkeystorendk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/liblocationndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libclipboardndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libpowerndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnotificationndk.so"
        "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_device.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_presentation.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_vulkan_compute_guest.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_audio_pcm.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_input_evdev.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_property_bionic.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_plugin.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_gles_quad.so"
        "${CMAKE_CURRENT_BINARY_DIR}/test_bionic_extensions_guest.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_aaudio_lifecycle.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_mediandk_pipeline.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_camera2_pipeline.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/test_cpp_runtime_guest.elf"
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/game_engine.apk"
           "${CMAKE_BINARY_DIR}/main.1.com.portage.gameengine.obb"
    COMMAND assemble_samples
        "${CMAKE_BINARY_DIR}/game_engine.apk"
        com.portage.gameengine
        test_game_engine
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_plugin.so"
    DEPENDS
        assemble_samples
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_game_engine.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_math.so"
        "${CMAKE_CURRENT_BINARY_DIR}/libgame_plugin.so"
    COMMENT "Assembling sample game_engine.apk with multi-library dependencies and OBB"
)

add_custom_target(sample_game_engine_apk ALL
    DEPENDS "${CMAKE_BINARY_DIR}/game_engine.apk"
            "${CMAKE_BINARY_DIR}/main.1.com.portage.gameengine.obb"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libz.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "-Wl,-soname,libz.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libz.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libz.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libz.c"
    COMMENT "Cross-compiling Android x86_64 shared library: libz.so"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libm.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -shared
        -fPIC
        -O2
        "-Wl,-soname,libm.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libm.c"
        -o "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libm.so"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libm.c"
    COMMENT "Cross-compiling Android x86_64 shared library: libm.so"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_showcase.so"
    COMMAND "${CMAKE_C_COMPILER}"
        --target=x86_64-linux-android
        -fuse-ld=lld
        -nostdlib
        -fno-stack-protector
        -shared
        -fPIC
        -O2
        "-L${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_ndk_showcase.c"
        -landroid
        -lnativewindow
        -ljnigraphics
        -lGLESv2
        -lz
        -lm
        -lc
        -o "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_showcase.so"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_ndk_showcase.c"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libandroid.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libnativewindow.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libjnigraphics.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libGLESv2.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libz.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libm.so"
        "${CMAKE_CURRENT_BINARY_DIR}/rootfs/system/lib64/libc.so"
    COMMENT "Cross-compiling Android x86_64 NDK showcase library: libtest_ndk_showcase.so"
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/ndk_showcase.apk"
    COMMAND assemble_samples
        --showcase
        "${CMAKE_BINARY_DIR}/ndk_showcase.apk"
        com.portage.ndkshowcase
        test_ndk_showcase
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_showcase.so"
    DEPENDS
        assemble_samples
        "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_showcase.so"
    COMMENT "Assembling NDK compatibility showcase APK: ndk_showcase.apk"
)

add_custom_target(sample_ndk_showcase_apk ALL
    DEPENDS "${CMAKE_BINARY_DIR}/ndk_showcase.apk"
)

if(PORTAGE_ANDROID_NDK_COMPILER AND PORTAGE_ANDROID_NDK_LIBCXX)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_cpp_showcase.so"
        COMMAND "${PORTAGE_ANDROID_NDK_COMPILER}"
            -std=c++20
            -shared
            -fPIC
            -fexceptions
            -frtti
            -O2
            "${CMAKE_CURRENT_SOURCE_DIR}/test_ndk_cpp_showcase.cpp"
            -llog
            -landroid
            -o "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_cpp_showcase.so"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/test_ndk_cpp_showcase.cpp"
        COMMENT "Cross-compiling Android x86_64 NDK C++20 showcase library: libtest_ndk_cpp_showcase.so"
    )

    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/ndk_cpp_showcase.apk"
        COMMAND assemble_samples
            --showcase
            "${CMAKE_BINARY_DIR}/ndk_cpp_showcase.apk"
            com.portage.ndkcppshowcase
            test_ndk_cpp_showcase
            "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_cpp_showcase.so"
            "${PORTAGE_ANDROID_NDK_LIBCXX}"
        DEPENDS
            assemble_samples
            "${CMAKE_CURRENT_BINARY_DIR}/libtest_ndk_cpp_showcase.so"
        COMMENT "Assembling NDK C++ STL showcase APK: ndk_cpp_showcase.apk"
    )

    add_custom_target(sample_ndk_cpp_showcase_apk ALL
        DEPENDS "${CMAKE_BINARY_DIR}/ndk_cpp_showcase.apk"
    )
endif()
