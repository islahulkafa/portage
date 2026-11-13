# ABI boundary and subsystem reference

This reference defines the SysV-to-Windows calling convention rules, dynamic linker symbol precedence, Bionic ABI constants, and all 29 subsystems under `src/`.
Required reading before adding native NDK thunks, Bionic extensions, or guest stub libraries.

## Calling convention boundary

Portage bridges two incompatible 64-bit calling conventions inside the same host process:

| Property | Host Windows (Microsoft x64) | Guest Android (System V AMD64) |
| :--- | :--- | :--- |
| **Integer Args** | `RCX`, `RDX`, `R8`, `R9` | `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9` |
| **Floating Args** | `XMM0`–`XMM3` | `XMM0`–`XMM7` |
| **Stack Shadow Space** | 32 bytes caller-allocated | None |
| **Red Zone** | None | 128 bytes below `%rsp` |

Every host function called by guest machine code—including every NDK thunk and every function pointer in `JNINativeInterface` and `JNIInvokeInterface`—must carry `__attribute__((sysv_abi))`:

```cpp
extern "C" __attribute__((sysv_abi)) ALooper *ALooper_prepare(int opts);
```

## Thunk registration and ABI invariants

- **Central Registry & Dual-Tier Resolution**: Register all native SysV NDK and Bionic thunks in `NdkSymbolRegistry` inside [libandroid_thunks.cpp](../src/ndk/libandroid_thunks.cpp). Symbols are partitioned into primary thunks and secondary C++ fallback stubs (`RegisterCppFallback()`).
- **Symbol Resolution Precedence**: `LinkMap::ResolveGlobalSymbol`, `LinkMap::ResolveSymbolForModule`, and `LinkMap::ResolveLazySymbol` in [link_map.cpp](../src/elf/link_map.cpp) enforce a 3-tier lookup hierarchy:
  1. Primary host SysV thunks in `NdkSymbolRegistry` (preventing no-op guest stub libraries like `libc.so` or `libandroid.so` from shadowing host implementations).
  2. Loaded physical ELF modules (e.g. real `libc++_shared.so` bundled in APKs).
  3. Secondary C++ fallback stubs in `NdkSymbolRegistry` (e.g. `_Znwm`, `_ZdlPv` when no STL `.so` is bundled).
- **Bionic stdio & VFS Integration**: `FILE*` streams are unified with Portage's VFS / `FileDescriptorTable`. Standard streams (`stdin`, `stdout`, `stderr`, `__sF[3]`) map directly to FDs 0, 1, and 2.
- **Bionic LP64 Structures & UTF-32**: Structures match official Bionic LP64 layouts verbatim:
  - `BionicDirent64` maintaining 280 bytes (`d_ino`, `d_off`, `d_reclen`, `d_type`, `d_name[256]`).
  - `BionicPthreadAttr` maintaining 56 bytes.
  - Wide-character APIs (`wint_t`, `wchar_t`) operate on 4-byte UTF-32 code points per Android Bionic standards, bridging to/from Win32 UTF-16 when necessary.
- **FSGSBASE & Stack Guard**: Guest x86_64 code compiled with `-fstack-protector` accesses `%fs:0x28`. Every host/guest thread manages a `BionicTcb` initialized by `ThreadManager::EnsureCurrentThreadTcb()` with `self` at offset 0 and canary at offset 0x28, guarded across context switches by Windows VEH.
- **Winpthreads Isolation**: Host MinGW/Clang C++ toolchains pull `<pthread.h>` into translation units. Name host C++ pthread implementations `portage_pthread_*` and bind standard `"pthread_*"` names inside `NdkSymbolRegistry`.
- **Unmodified Bionic Struct & Bitmask Parity**: Match AOSP / Android NDK LP64 definitions verbatim:
  - `ANDROID_DLEXT_*` bitmasks in [elf_types.hpp](../src/elf/elf_types.hpp) (`ANDROID_DLEXT_RESERVED_ADDRESS = 0x1`, `ANDROID_DLEXT_USE_LIBRARY_FD = 0x10`, `ANDROID_DLEXT_USE_LIBRARY_FD_OFFSET = 0x20`, `ANDROID_DLEXT_USE_NAMESPACE = 0x200`).
  - `BionicPthreadAttr` in [libandroid_thunks.hpp](../src/ndk/libandroid_thunks.hpp) maintaining the exact 56-byte Bionic LP64 layout.
- **Value-Copyable `LoadedElf`**: Keep [`LoadedElf`](../src/elf/elf_loader.hpp) copyable by value (e.g., store relocation lifetime objects such as `tlsdesc_args` in `std::shared_ptr`) so launcher and test fixtures can register copied instances cleanly.
- **Header Include Hygiene**: Use `#pragma once` and root-relative `#include` paths from `src/` (for example `#include "ndk/looper.hpp"`); omit parent traversal (`../`).

## Add a new NDK or Bionic thunk

1. Implement the host entrypoint decorated with `extern "C" __attribute__((sysv_abi))` in the appropriate subsystem (`src/ndk/`, `src/elf/`, `src/media/`, etc.).
2. Register the exported symbol name and thunk pointer inside `NdkSymbolRegistry::RegisterAll()` in [libandroid_thunks.cpp](../src/ndk/libandroid_thunks.cpp).
3. Export a matching C stub symbol in the corresponding guest stub library under `tests/guests/` (for example [libc.c](../tests/guests/libc.c), [libdl.c](../tests/guests/libdl.c), or [libandroid.c](../tests/guests/libandroid.c)) so guest binaries link against the symbol cleanly.

<details>
<summary><strong>View all 20 guest stub libraries under tests/guests/</strong></summary>

- [libaaudio.c](../tests/guests/libaaudio.c)
- [libandroid.c](../tests/guests/libandroid.c)
- [libbinder_ndk.c](../tests/guests/libbinder_ndk.c)
- [libc.c](../tests/guests/libc.c)
- [libcamera2ndk.c](../tests/guests/libcamera2ndk.c)
- [libclipboardndk.c](../tests/guests/libclipboardndk.c)
- [libdl.c](../tests/guests/libdl.c)
- [libEGL.c](../tests/guests/libEGL.c)
- [libGLESv2.c](../tests/guests/libGLESv2.c)
- [libjnigraphics.c](../tests/guests/libjnigraphics.c)
- [libkeystorendk.c](../tests/guests/libkeystorendk.c)
- [liblocationndk.c](../tests/guests/liblocationndk.c)
- [libmediandk.c](../tests/guests/libmediandk.c)
- [libnativewindow.c](../tests/guests/libnativewindow.c)
- [libnotificationndk.c](../tests/guests/libnotificationndk.c)
- [libOpenSLES.c](../tests/guests/libOpenSLES.c)
- [libpowerndk.c](../tests/guests/libpowerndk.c)
- [libswappy_gl.c](../tests/guests/libswappy_gl.c)
- [libswappy_vk.c](../tests/guests/libswappy_vk.c)
- [libvulkan.c](../tests/guests/libvulkan.c)

</details>

## Runtime subsystem directory catalog

All core runtime components reside under `src/`:

| Directory | Subsystem Responsibility |
| :--- | :--- |
| `src/arch/` | Guest CPU state, stack frame allocation, registers, and execution dispatch |
| `src/audio/` | WASAPI audio backend, AAudio stream dispatcher, and OpenSL ES engine |
| `src/binder/` | Android IPC Binder driver emulation and `ioctl` parcel handlers |
| `src/camera/` | Camera2 NDK (`ACameraManager` availability callbacks, SysV capture/sequence callbacks, `WmfCameraReader` live WMF capture with synthetic fallback, WIC JPEG `AImageReader`, and direct `ANativeWindow` preview) |
| `src/clipboard/` | Clipboard manager, MIME clip data, and Win32 OLE clipboard integration |
| `src/elf/` | ELF64 parser, `R_X86_64_TLSDESC`, `R_X86_64_IRELATIVE` IFUNC, symbol versioning (`DT_VERNEED`/`DT_VERSYM`), lazy PLT trampolines, `android_dlopen_ext`, `dlinfo`, `dlclose`, virtual system library fallback, unresolved symbol trap stubs, and `LinkMap` |
| `src/fs/` | Virtual File System (VFS), `/proc` (`self/{smaps,smaps_rollup,status}`), `/sys` (cgroups v2, CPU topology), `/dev`, `inotify`, symlinks/hardlinks, file descriptor tables, `SyncFence`, and pipes |
| `src/gles/` | OpenGL ES 2.0–3.2 passthrough, EGL 1.5 dispatcher, and WGL bridge |
| `src/input/` | Evdev input translation, gamepad/haptics state, and Win32 touch/key mapping |
| `src/launcher/` | CLI runner (`portage.exe`), argument parsing, `--check-apk` audit, APK bootstrap (`ANativeActivity`, pure-DEX, and hybrid Java-native), and event loop |
| `src/location/` | `LocationManager`, simulated coordinates, and Windows Location API |
| `src/media/` | `AMediaCodec` (`IMFTransform` encode/decode, async notify worker, NV12/RGBA surface bridging), byte-range `AMediaExtractor`, and `AMediaMuxer` (`IMFSinkWriter` MP4/3GP) over Windows Media Foundation COM MTA dispatch |
| `src/mem/` | Virtual memory manager, address space mappings (`mmap`/`mprotect`/`mremap`), and shared memory |
| `src/ndk/` | NDK thunks (`libandroid`, `ALooper`, `AInputQueue`, `AConfiguration`, `liblog`, `AChoreographer`, `ANativeWindow_fromSurface`, `AHardwareBuffer`, WIC `AImageDecoder`, LP64 `libz`, `libm`, Bionic stdio/POSIX/locale/UTF-32 wchar runtime, `sched_*affinity`, `getifaddrs`/`if_nametoindex`, fortify/`pthread_create`, and C++ fallback stubs) |
| `src/net/` | BSD/Linux socket translation, loopback networking, and Netlink route sockets (`AF_NETLINK`, `NETLINK_ROUTE`, `RTM_GETLINK`/`RTM_GETADDR`/`RTM_GETROUTE`) |
| `src/notification/` | Notification channels (`NotificationManager`) and Win32 `Shell_NotifyIconW` |
| `src/package/` | APK ZIP extractor, binary XML, `ManifestParser` (`MAIN`/`LAUNCHER`, `Application`), `resources.arsc`, `AAssetManager`, `ContentProvider`/SAF, `DexFile` (v035–v039 multi-DEX), `DexVm` (register-based Dalvik interpreter, `Looper`/`Handler`/`SurfaceView`/`Canvas`, bidirectional SysV-ABI `ACC_NATIVE`/`RegisterNatives`), `JavaActivityRunner`, `JNIEnv` (WIC `BitmapFactory`, `SharedPreferences`, `DisplayMetrics`, `PackageManager`, `Build`), and `ANativeActivity` |
| `src/poll/` | Linux `epoll`, `eventfd`, `timerfd`, and waitable `SyncFence` polling |
| `src/power/` | `PowerManager`, `WakeLock` (`SetThreadExecutionState`), and battery simulation |
| `src/process/` | Guest process lifecycle, process tree, Linux namespaces (`NamespaceManager`), and PID/credential state |
| `src/property/` | Android system properties (`/dev/__properties__`), trie backing, and property services |
| `src/rewrite/` | `syscall` instruction rewriting and `x86_64` instruction decoding |
| `src/security/` | Android Keystore / KeyStore2 backed by Windows CNG (`NCrypt` / `BCrypt`) |
| `src/signal/` | Signal dispatching, `sigaction`, alternate signal stacks, and Win32 VEH recovery |
| `src/sync/` | Linux `futex` wait queues, fast userspace locking, and robust lists |
| `src/syscall/` | Linux syscall table, argument extraction, and subsystem dispatch |
| `src/thread/` | Bionic `ThreadManager`, descriptors, per-thread names/stack bounds, TLS/FSGSBASE, and joins |
| `src/vulkan/` | Vulkan 1.0–1.3 ICD passthrough, external sync FDs, `AHardwareBuffer` memory emulation, and swapchain WSI |
| `src/wsi/` | Window System Integration, Win32 window creation, message pump, and event loop |
