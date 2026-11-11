# Get started with Portage

Build Portage from source on Windows 11, provision an optional Android x86_64 rootfs, and execute your first APK or standalone Bionic ELF binary.
Requires an x86_64 Windows 11 host CPU supporting `FSGSBASE` (`-mfsgsbase`).

## Prerequisites

Install the build and runtime dependencies declared in [CMakeLists.txt](../CMakeLists.txt), [CMakePresets.json](../CMakePresets.json), and [Dependencies.cmake](../cmake/Dependencies.cmake):

- **Compiler & Build System**: Clang / Clang++ (`C++20`), Ninja, and CMake `3.25` or newer.
- **Libraries**: MSYS2 UCRT64 `zlib` and `gtest` (CMake falls back to `C:/Dev/msys64/ucrt64` when `ZLIB_ROOT` is unset).
- **Vulkan Headers**: Vulkan SDK installed with `VULKAN_SDK` set (CMake falls back to `C:/Dev/VulkanSDK/1.4.363.0/Include`).
- **Optional NDK Sysroot**: Android NDK (or Python 3) if running arbitrary external Android binaries that expect NDK `.so` libraries in `rootfs/`.

> [!IMPORTANT]
> Portage executes native Android `x86_64` code directly on the Windows user-mode CPU without instruction translation. Do not attempt to run `armeabi-v7a` or `arm64-v8a` binaries; ensure your guest ELF or APK bundles `x86_64` native libraries.

## Build from source

1. Configure the Ninja Debug build preset:

   ```powershell
   cmake --preset debug
   ```

2. Build the runtime binary (`portage.exe`), sample APK (`game_engine.apk`), guest test fixtures, and test suites:

   ```powershell
   cmake --build build/debug
   ```

3. For an optimized binary without test targets, build the `release` preset instead:

   ```powershell
   cmake --preset release
   cmake --build build/release
   ```

## Prepare an Android rootfs (optional)

When executing standalone Android binaries outside the built-in test suite, populate `rootfs/` with NDK `x86_64` system libraries using [setup_android_rootfs.py](../.github/scripts/setup_android_rootfs.py):

```powershell
python .github/scripts/setup_android_rootfs.py --output rootfs
```

The script creates `system/bin/`, `system/lib64/`, `vendor/lib64/`, and `data/local/tmp/` under `rootfs/`, and copies `libc.so`, `libm.so`, `libdl.so`, and `liblog.so` when it detects an Android NDK under `%LOCALAPPDATA%\Android\Sdk\ndk` or via `--ndk <path>`.

## Run your first APK and binary

1. Run the bundled native activity sample APK in headless mode:

   ```powershell
   .\build\debug\src\portage.exe --headless run build\debug\game_engine.apk
   ```

   Expected output:

   ```text
   [I/PortageEngine] ANativeActivity_onCreate started
   [I/PortageEngine] AConfiguration orientation=2
   [I/PortageEngine] ANativeActivity_onCreate finished
   [I/PortageEngine] onStart
   [I/PortageEngine] onResume
   [I/PortageEngine] onNativeWindowCreated
   [I/PortageEngine] onInputQueueCreated
   [I/PortageEngine] onWindowFocusChanged hasFocus=1
   [I/PortageEngine] onPause
   [I/PortageEngine] onNativeWindowDestroyed
   [I/PortageEngine] onInputQueueDestroyed
   [I/PortageEngine] onStop
   [I/PortageEngine] onDestroy
   [I/PortageEngine] Worker thread started and ALooper prepared
   [I/PortageEngine] Worker received cmd=1
   [I/PortageEngine] Worker received cmd=6
   [I/PortageEngine] Worker received cmd=15
   [I/PortageEngine] Worker thread terminating
   [portage] APK native activity execution completed successfully
   ```

2. Launch any standalone `x86_64` ELF binary against your prepared `rootfs/`:

   ```powershell
   .\build\debug\src\portage.exe --rootfs rootfs --verbose run path\to\guest_binary
   ```

3. Run an interactive windowed APK session by omitting `--headless` (close the Win32 window to exit cleanly).

For available flags and environment variables, see the [CLI reference](./cli-reference.md).
To run automated unit and integration tests, see [Verify and test Portage](./testing.md).
