# AGENTS.md

## Repository Identity & Core Architecture

Portage is a native user-mode Android compatibility layer for Windows 11 written in modern C++20. It executes unmodified Android x86_64 Bionic binaries and APK packages directly on Windows without full-system emulation, virtual machines, or translation overhead.

### Architectural Pillars
- **Direct CPU Execution**: Guest 64-bit code executes natively on the host x86_64 CPU. Fast thread-local storage switching is achieved via `-mfsgsbase` (`rdfsbase`/`wrfsbase`).
- **Syscall Interception & Translation**: Bionic Linux system calls are trapped, decoded, and dispatched to Windows Win32, NT, or POSIX-compatible emulation layers (`SyscallDispatcher`).
- **Dynamic Linker & ABI Bridging**: The custom ELF dynamic linker (`LinkMap`, `ElfLoader`) loads guest shared libraries, resolves recursive `DT_NEEDED` dependencies, executes `DT_INIT`/`DT_INIT_ARRAY`, and routes guest function calls across the ABI boundary (SysV AMD64 ABI to Microsoft x64 ABI) via SysV-annotated thunks (`NdkSymbolRegistry`).
- **Graphics Pipeline (WSI & WGL)**: Provides hardware-accelerated OpenGL ES 2.0 / 3.0 / 3.2 and Vulkan passthrough directly to host GPUs, presenting frames to Win32 `HWND` surfaces or offscreen DIB sections.
- **Audio Pipeline**: Emulates Android NDK AAudio (API 26+) and OpenSL ES interfaces over low-latency Windows WASAPI streaming audio.
- **Android Package & Resource Runtime**: Parses APK ZIP archives, extracts sandboxed assets, decodes binary XML (`AndroidManifest.xml`), parses binary resource tables (`resources.arsc`), resolves string/integer/boolean resources, and hosts synthetic Android Java runtime environments (`JavaVM`, `JNIEnv`).

---

## Subsystem Directory Layout

All source code resides in `src/`, with subsystems structured as follows:

| Directory | Subsystem Description |
| :--- | :--- |
| `src/arch/` | Guest CPU state, stack frame allocation, registers, and execution dispatch |
| `src/audio/` | WASAPI audio backend, AAudio stream dispatcher, and OpenSL ES engine |
| `src/binder/` | Android IPC Binder driver emulation and ioctl handlers |
| `src/elf/` | ELF64 binary parsing, segment loading, `LinkMap` management, and relocations |
| `src/fs/` | Virtual File System (VFS), `/proc`, `/dev`, file descriptor tables, and pipes |
| `src/gles/` | OpenGL ES 2.0 / 3.0 passthrough, EGL dispatcher, and WGL bridge |
| `src/input/` | Evdev input translation, Win32 mouse/keyboard to Android touch/key mapping |
| `src/launcher/` | CLI runner (`portage.exe`), argument parsing, APK installation, and execution |
| `src/mem/` | Virtual memory manager, address space mappings (`mmap`/`mprotect`), and page tracking |
| `src/ndk/` | Android NDK runtime libraries (`libandroid`, `ALooper`, `AInputQueue`, `AConfiguration`, `liblog`, `AChoreographer`) |
| `src/net/` | Socket manager, BSD/Linux socket emulation, and loopback networking |
| `src/package/` | APK ZIP archive extractor, binary XML parser, `resources.arsc` table, `AAssetManager`, `JNIEnv`, and `ANativeActivity` |
| `src/poll/` | Linux `epoll` subsystem and waitable event polling |
| `src/process/` | Guest process lifecycle, process tree, and PID tracking |
| `src/property/` | Android system properties (`/dev/__properties__`), trie structure, and property services |
| `src/rewrite/` | Syscall instruction rewriting and x86_64 instruction decoding |
| `src/signal/` | Signal dispatching, `sigaction`, signal masks, and SEGV recovery via Win32 VEH |
| `src/sync/` | Linux `futex` emulation, fast userspace locking, and wait queues |
| `src/syscall/` | Linux syscall table, syscall routing, and argument extraction |
| `src/thread/` | Bionic thread manager, thread descriptors, TLS/FSGSBASE, and thread joins |
| `src/vulkan/` | Vulkan ICD passthrough and swapchain WSI integration |
| `src/wsi/` | Window System Integration, Win32 window creation, message pump, and event loop |

Test and tool layout:
- `tests/guests/`: Unmodified guest source code compiled into ELF/shared library test fixtures (e.g. `test_game_engine.c`, `libandroid.c`).
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
3. **Thunk Symbol Registration**:
   - All SysV NDK thunks implemented in `src/ndk/` must be registered in `NdkSymbolRegistry` in `src/ndk/libandroid_thunks.cpp`.
   - The corresponding symbol stub must be exported in `tests/guests/libandroid.c` to permit guest link-time resolution.

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
