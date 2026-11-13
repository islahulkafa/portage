# AGENTS.md

## Repository Identity & Core Architecture

Portage is a native user-mode Android compatibility layer for Windows 11 written in modern C++20. It executes unmodified Android x86_64 Bionic binaries and APK packages directly on Windows without full-system emulation, virtual machines, or translation overhead.

### Architectural Pillars
- **Direct CPU Execution**: Guest 64-bit code executes natively on the host x86_64 CPU. Fast thread-local storage switching is achieved via `-mfsgsbase` (`rdfsbase`/`wrfsbase`).
- **Syscall Interception & Translation**: Bionic Linux system calls are trapped, decoded, and dispatched to Windows Win32, NT, or POSIX-compatible emulation layers (`SyscallDispatcher`).
- **Dynamic Linker & ABI Bridging**: The custom ELF dynamic linker (`LinkMap`, `ElfLoader`) loads guest shared libraries, resolves recursive `DT_NEEDED` dependencies, executes `DT_INIT`/`DT_INIT_ARRAY`/`DT_FINI`/`DT_FINI_ARRAY` with `__cxa_atexit` tracking, supports GNU2 TLSDESC (`R_X86_64_TLSDESC`), lazy `DT_JMPREL` PLT trampolines, `android_dlopen_ext`, `dlinfo`, `dlclose` segment unmapping, and routes guest calls across the ABI boundary (SysV AMD64 ABI to Microsoft x64 ABI) via SysV-annotated thunks (`NdkSymbolRegistry`).
- **Graphics Pipeline (WSI & WGL)**: Provides hardware-accelerated OpenGL ES 2.0 / 3.0 / 3.2 (EGL 1.5, sync fences, `AHardwareBuffer` textures) and Vulkan 1.0–1.3 ICD passthrough directly to host GPUs (including external `SyncFence` FD and `AHardwareBuffer` memory bridging), presenting frames to Win32 `HWND` surfaces or offscreen DIB sections.
- **Audio & Media Pipelines**: Emulates Android NDK AAudio (API 26+) and OpenSL ES over low-latency Windows WASAPI streaming audio; bridges MediaCodec, MediaExtractor, and Camera2 via Windows Media Foundation.
- **Android Package & Resource Runtime**: Parses APK ZIP archives, extracts sandboxed assets, decodes binary XML (`AndroidManifest.xml`), parses binary resource tables (`resources.arsc`), resolves string/integer/boolean resources, parses Dalvik DEX v035–v039 (`DexFile`) with multi-DEX loading, runs Java `Application`/`Activity` lifecycles (`JavaActivityRunner`) on a register-based Dalvik bytecode interpreter (`DexVm`), and bridges `System.loadLibrary` / `JNI_OnLoad`, `SurfaceView` (`ANativeWindow_fromSurface`), and bidirectional `JavaVM`/`JNIEnv` dispatch.
- **Host Desktop & System Integration**: Bridges Android system services (Power/WakeLocks via `SetThreadExecutionState`, Battery status, Clipboard via OLE, Location via Windows `LocationAPI`, Notifications/Toasts via `Shell_NotifyIconW`, and Keystore via Windows CNG `NCrypt`/`BCrypt`).

---

## Subsystem Directory Layout

All source code resides in `src/`, with subsystems structured as follows:

| Directory | Subsystem Description |
| :--- | :--- |
| `src/arch/` | Guest CPU state, stack frame allocation, registers, and execution dispatch |
| `src/audio/` | WASAPI polling and event-driven low-latency backend (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`, `IAudioClock`, XRun detection), AAudio dispatcher (`requestFlush`, `waitForStateChange`, `getFramesWritten`/`Read`, `getTimestamp`, `getXRunCount`, usage/content-type/input-preset hints, `getPerformanceMode`/`SharingMode`/`DeviceId`), and OpenSL ES engine (`SLMuteSoloItf`, `SLAndroidConfigurationItf`) |
| `src/binder/` | Android IPC Binder driver emulation and ioctl handlers |
| `src/camera/` | Camera2 NDK (`ACameraManager` availability callbacks, `ACameraDevice`, SysV capture/sequence callbacks, `AImageReader` with WIC JPEG encoding, direct `ANativeWindow` preview blitting, and live WMF `IMFSourceReader` capture with synthetic color-bar fallback) |
| `src/clipboard/` | Clipboard manager (`ClipboardManager`), MIME clip data, and Win32 OLE clipboard integration |
| `src/elf/` | ELF64 parsing, segment loading, `R_X86_64_TLSDESC`, `R_X86_64_IRELATIVE` IFUNC, symbol versioning (`DT_VERNEED`/`DT_VERSYM`), lazy PLT trampolines, `android_dlopen_ext`, `dlinfo`, `dlclose` unmapping, virtual system library fallback (`libz`, `libm`, `libc++_shared`, NDK libs), unresolved symbol tracking/trap stubs, and `LinkMap` management |
| `src/fs/` | Virtual File System (VFS), `/proc` (`self/{smaps,smaps_rollup,status}`), `/sys` (cgroups v2, CPU topology), `/dev`, `inotify`, symlinks/hardlinks, file descriptor tables, `SyncFence`, and pipes |
| `src/gles/` | OpenGL ES 2.0–3.2 passthrough, EGL 1.5 dispatcher, and WGL bridge |
| `src/input/` | Evdev input translation, Win32 mouse/keyboard to Android touch/key mapping |
| `src/launcher/` | CLI runner (`portage.exe`), argument parsing, `--check-apk` symbol audit, APK installation, and `ANativeActivity`, pure-DEX, and hybrid Java-native (`System.loadLibrary` + `JNI_OnLoad`) execution |
| `src/location/` | Android location provider (`LocationManager`), GPS/network emulation, and Windows Location API |
| `src/media/` | MediaCodec (`IMFTransform` H.264/HEVC/AAC encode/decode, async notify callbacks, input/output `ANativeWindow` NV12/RGBA surfaces), byte-range MediaExtractor (`PortageMfByteStream`), and MediaMuxer (`IMFSinkWriter` MP4/3GP) over a dedicated Windows Media Foundation COM MTA host worker |
| `src/mem/` | Virtual memory manager, address space mappings (`mmap`/`mprotect`), and page tracking |
| `src/ndk/` | Android NDK runtime libraries (`libandroid`, `ALooper`, `AInputQueue`, `AConfiguration`, `liblog`, `AChoreographer`, `ANativeWindow_fromSurface`, WIC-backed `AImageDecoder`, LP64 `GuestZStream` `libz`, `libm`, Bionic stdio/POSIX/locale/UTF-32 wchar runtime, `sched_*affinity`, `getifaddrs`/`if_nametoindex`, fortify/`pthread_create`, and C++ fallback stubs) |
| `src/net/` | Socket manager, BSD/Linux socket emulation, loopback networking, and Netlink route sockets (`AF_NETLINK`, `NETLINK_ROUTE`, `RTM_GETLINK`/`RTM_GETADDR`/`RTM_GETROUTE`) |
| `src/notification/` | Notification & Toast subsystem (`NotificationManager`, `NotificationChannel`), and Win32 `Shell_NotifyIconW` |
| `src/package/` | APK ZIP archive extractor, binary XML parser, `ManifestParser` (`MAIN`/`LAUNCHER`, `Application`), `resources.arsc` table, `AAssetManager`, `ContentProvider`/SAF, `DexFile` (v035–v039 multi-DEX), `DexVm` (register-based Dalvik interpreter, `Looper`/`Handler`/`SurfaceView`/`Canvas`, bidirectional SysV-ABI `ACC_NATIVE`/`RegisterNatives`), `JavaActivityRunner`, `JNIEnv` (WIC `BitmapFactory`, `SharedPreferences`, `DisplayMetrics`, `PackageManager`, `Build`), and `ANativeActivity` |
| `src/poll/` | Linux `epoll`, `eventfd`, `timerfd`, and waitable `SyncFence` event polling |
| `src/power/` | Power management (`PowerManager`, `WakeLock`), Win32 `SetThreadExecutionState`, and battery status |
| `src/process/` | Guest process lifecycle, process tree, and PID tracking |
| `src/property/` | Android system properties (`/dev/__properties__`), trie structure, and property services |
| `src/rewrite/` | Syscall instruction rewriting and x86_64 instruction decoding |
| `src/security/` | Android Keystore / KeyStore2 subsystem and Windows CNG (`NCrypt`/`BCrypt`) cryptographic provider |
| `src/signal/` | Signal dispatching, `sigaction`, signal masks, and SEGV recovery via Win32 VEH |
| `src/sync/` | Linux `futex` emulation, fast userspace locking, and wait queues |
| `src/syscall/` | Linux syscall table, syscall routing, and argument extraction |
| `src/thread/` | Bionic thread manager, thread descriptors, per-thread names/stack bounds, TLS/FSGSBASE, and thread joins |
| `src/vulkan/` | Vulkan 1.0–1.3 ICD passthrough, external sync FDs, `AHardwareBuffer` memory emulation, and swapchain WSI integration |
| `src/wsi/` | Window System Integration, Win32 window creation, message pump, and event loop |

Test and tool layout:
- `tests/guests/`: Unmodified guest source code and stub libraries compiled into ELF/shared library test fixtures (e.g. `test_game_engine.c`, `test_java_hybrid_guest.c`, `test_bionic_extensions_guest.c`, `libc.c`, `libdl.c`, `libvulkan.c`, `libGLESv2.c`, `libandroid.c`, `libnotificationndk.c`, `libpowerndk.c`, `libmediandk.c`).
- `tests/unit/`: GoogleTest unit test suites testing isolated subsystems.
- `tests/integration/`: End-to-end integration test suites executing guest code through the emulator runtime.
- `tools/`: Build and development utilities, including `assemble_samples.cpp` for generating test APKs.

---

## Build, Test & Verification Standards

Maintain strict command stability across all agent invocations. Do not mutate compiler flags or switch generators unless explicitly requested.

### Standard Build Commands
```powershell
# Configure Debug build with Ninja
cmake --preset debug

# Build all targets in Debug mode
cmake --build build/debug
```

### Strict Verification Hierarchy
Before declaring any task or milestone complete, agents must execute the verification hierarchy and inspect direct command outputs:

1. **Compilation Gate**:
   ```powershell
   cmake --build build/debug
   ```
   The build must succeed with zero warnings and zero errors.

2. **Targeted Unit Test Gate**:
   ```powershell
   .\build\debug\tests\unit_tests.exe --gtest_filter=<TargetTestSuite>.*
   ```

3. **Targeted Integration Test Gate**:
   ```powershell
   .\build\debug\tests\integration_tests.exe --gtest_filter=<TargetIntegrationSuite>.*
   ```

4. **Complete CTest Gate**:
   ```powershell
   ctest --test-dir build/debug --output-on-failure
   ```
   100% of all registered tests must pass. Zero failures permitted.

5. **Live Launcher Verification Gate**:
   ```powershell
   .\build\debug\src\portage.exe --headless run build\debug\game_engine.apk
   ```
   Verify guest engine lifecycle, JNI initialization, asset reading, and clean exit.

---

## ABI & Calling Convention Invariants

Portage operates across two distinct 64-bit calling conventions on x86_64:
- **Host Windows**: Microsoft x64 ABI (registers: `RCX`, `RDX`, `R8`, `R9`; 32-byte shadow space on stack).
- **Guest Android**: System V AMD64 ABI (registers: `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`; no shadow space, red zone permitted).

### Mandatory ABI Rules
1. **Explicit Attribute Decoration**: All native functions exposed to or called by guest code must be decorated with `__attribute__((sysv_abi))`.
   ```cpp
   // Valid guest callback / NDK function signature:
   extern "C" __attribute__((sysv_abi)) ALooper *ALooper_prepare(int opts);
   ```
2. **JNI Function Pointers**: Every function pointer inside `JNINativeInterface` and `JNIInvokeInterface` must use `__attribute__((sysv_abi))`. Mismatches cause catastrophic stack corruption and silent crash regressions.
3. **Thunk Symbol Registration & Lookup Precedence**:
   - All SysV NDK and Bionic thunks implemented in `src/ndk/` or subsystem directories (`src/elf/`, `src/notification/`, `src/power/`, `src/clipboard/`, `src/location/`, `src/camera/`, `src/security/`, `src/media/`) must be registered in `NdkSymbolRegistry` in `src/ndk/libandroid_thunks.cpp`.
   - Dual-tier resolution: `NdkSymbolRegistry` maintains primary thunks and secondary C++ fallback stubs (`RegisterCppFallback()`). `LinkMap` symbol lookup precedence is:
     1. Primary host SysV thunks in `NdkSymbolRegistry` (preventing no-op guest stub libraries such as `libc.so`, `libdl.so`, `libandroid.so` from shadowing real host implementations).
     2. Loaded physical ELF modules (e.g. real `libc++_shared.so` bundled with APKs).
     3. Secondary C++ fallback stubs in `NdkSymbolRegistry` (e.g. `_Znwm`, `_ZdlPv` when no C++ STL `.so` is bundled).
   - The corresponding symbol stubs must be exported in guest stub libraries under `tests/guests/` (e.g. `libc.c`, `libdl.c`, `libandroid.c`, `libvulkan.c`, `libEGL.c`, `libGLESv2.c`, `libnotificationndk.c`, `libpowerndk.c`, `liblocationndk.c`, `libclipboardndk.c`, `libcamera2ndk.c`, `libkeystorendk.c`, `libmediandk.c`) to permit guest link-time resolution.
4. **Host/Guest ABI Layout & Symbol Isolation**:
   - Bionic ABI constants (`ANDROID_DLEXT_*` bitmasks, `RTLD_DI_*` requests, `BionicPthreadAttr` 56-byte LP64 layout) must match official Android NDK / AOSP definitions verbatim for unmodified APK compatibility.
   - Prefix host C++ `pthread` thunk implementations as `portage_pthread_*` (registered under `"pthread_*"` in `NdkSymbolRegistry`) to avoid symbol collisions with MinGW/Clang host `<pthread.h>`.
   - Keep `LoadedElf` copyable by value (e.g., use `std::shared_ptr` for shared relocation state such as `tlsdesc_args`) so launcher and runtime registration remain copy-safe.

---

## C++20 Architecture & Code Guidelines

### Standards & Idioms
- **C++20 Conformance**: Leverage modern features: `std::span`, `std::string_view`, structured bindings, and designated initializers.
- **Header Include Hygiene**:
  - Use `#pragma once` for all header files.
  - All project headers must be included relative to `src/` (root-relative), for example `#include "ndk/looper.hpp"`.
  - Strictly forbid parent directory traversal (`#include "../../ndk/looper.hpp"`).
- **Memory Ownership & RAII**:
  - Prefer value semantics and standard containers (`std::vector`, `std::string`, `std::unordered_map`).
  - Manage dynamic resources exclusively via `std::unique_ptr` or `std::shared_ptr`.
  - Forbid unowned raw `new`/`delete` outside dedicated custom memory allocators.
- **Concurrency & Synchronization**:
  - Use `std::mutex`, `std::condition_variable`, and `std::atomic` for cross-thread coordination.
  - Strictly forbid busy-wait spin loops or thread sleeps for state synchronization; use event-driven signaling.

---

## Repository Operational Non-Negotiables

### Zero AI Slop & Cleanliness
- **Surgical Edits**: Make only the minimum necessary changes to accomplish the task. Never refactor untouched lines or churn files arbitrarily.
- **Precision Tooling**: Use `edit_file` for targeted changes on existing files. Never overwrite existing files completely with `write_file` unless executing an authorized total replacement.
- **No Speculative Abstractions**: Do not introduce unnecessary wrapper classes, factories, or indirection layers. Implement direct, idiomatic logic.
- **No Placeholders or Stubs**: Never commit `TODO`, `FIXME`, or placeholder mock logic in production code. Write fully working implementations.
- **Strict Comment Restraint**: Write self-documenting code. Strictly forbid comments with numbers or step markers (e.g., `// 1.`, `// Step 2:`).
- **Code Formatting**: Conform strictly to `.clang-format` (LLVM base, 4-space indentation, braces wrapped after functions, 100 column limit).
- **Zero Emojis**: Strictly forbid emojis in all agent chat outputs, markdown documents, comments, and commit messages.
- **Case Discipline**: Forbid writing in all-uppercase for emphasis or shouting (e.g. do not write "DO NOT", "ALWAYS", "NEVER"). Use bolding or sentence case.

### Git Commit Standards
- **No Unsolicited Commits**: Never run `git commit`, `git add`, or stage changes without explicit instruction or prior confirmation from the user.
- **Unified Single-Line Commits**: When a commit is requested, commit all workspace changes into a single unified commit with a single-line imperative header (e.g. `Implement binary resource table parser and asset pipeline`). Strictly forbid conventional commit prefixes (`feat:`, `fix:`) and forbid commit bodies.
