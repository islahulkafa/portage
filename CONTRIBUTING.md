# Contributing to Portage

Portage is a native user-mode Android compatibility layer for Windows 11 written in modern C++20. Contributions covering syscall coverage, Bionic/NDK thunks, graphics/media backends, package runtime features, and automated tests are welcome.

---

## Development Environment

### Prerequisites
- **Operating System**: Windows 11 (x86_64)
- **Compiler**: Clang (`clang` / `clang++`) supporting C++20 and `-mfsgsbase`
- **Build System**: CMake (>= 3.25) and Ninja
- **Vulkan SDK**: Installed with `VULKAN_SDK` set (required for Vulkan headers)
- **Android NDK**: Installed with `ANDROID_NDK_ROOT` or `ANDROID_NDK_HOME` set, or under `%LOCALAPPDATA%\Android\Sdk\ndk\*` (required to cross-compile guest x86_64 ELF and `.so` test fixtures)
- **GoogleTest**: Required when `BUILD_TESTING=ON` (enabled by default in the `debug` preset)

### Configure & Build
Use the repository's CMake presets in `CMakePresets.json`:

```powershell
# Configure Debug build with Ninja and tests enabled
cmake --preset debug

# Build all host libraries, launcher, guest fixtures, and tests
cmake --build build/debug
```

For an optimized build without tests:

```powershell
cmake --preset release
cmake --build build/release
```

---

## Verification Workflow

All pull requests must pass the full verification suite with zero compiler warnings and zero test failures:

1. **Compilation Gate**:
   ```powershell
   cmake --build build/debug
   ```
2. **Targeted Unit Tests**:
   ```powershell
   .\build\debug\tests\unit_tests.exe --gtest_filter=<TestSuite>.*
   ```
3. **Targeted Integration Tests**:
   ```powershell
   .\build\debug\tests\integration_tests.exe --gtest_filter=<FixtureName>.*
   ```
4. **Complete CTest Suite**:
   ```powershell
   ctest --test-dir build/debug --output-on-failure
   ```
5. **Live Launcher Smoke Check**:
   ```powershell
   .\build\debug\src\portage.exe --headless run build\debug\game_engine.apk
   ```

---

## Coding Standards & Architecture Invariants

### ABI Boundary Discipline
Portage bridges two incompatible 64-bit calling conventions on x86_64:
- **Host Windows**: Microsoft x64 ABI (`RCX`, `RDX`, `R8`, `R9`, 32-byte shadow space)
- **Guest Android**: System V AMD64 ABI (`RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`, no shadow space)

Every function exposed to or invoked by guest code **must** be explicitly decorated with `__attribute__((sysv_abi))`. This applies to:
- NDK and Bionic thunk functions
- All function pointers inside `JNINativeInterface` and `JNIInvokeInterface`
- Guest-registered callback function pointer types (for example, `AThermal_StatusCallback`)

### C++20 & Formatting
- **Style**: Format all C/C++ code according to [`.clang-format`](.clang-format) (LLVM base, 4-space indentation, 100-column limit, braces wrapped after function definitions).
- **Headers**: Use `#pragma once` and root-relative includes from `src/` (e.g., `#include "ndk/looper.hpp"`). Do not use relative parent traversal (`../`).
- **Ownership**: Prefer value semantics, standard containers, `std::span`, `std::string_view`, and RAII (`std::unique_ptr`, `std::shared_ptr`). Avoid unowned raw `new`/`delete` outside dedicated allocators.
- **Concurrency**: Coordinate threads using `std::mutex`, `std::condition_variable`, and `std::atomic`. Avoid busy-wait spin loops or sleep polling.
- **Cleanliness**: Do not commit `TODO`, `FIXME`, or stubbed mock implementations in production code.

---

## Adding Syscalls and NDK / Bionic Symbols

### Adding a Linux x86_64 Syscall
1. Define any required Linux ABI structs or error/flag constants in `src/syscall/linux_types.hpp` or `src/syscall/syscall_dispatcher.hpp`.
2. Implement the syscall case in `SyscallDispatcher::Dispatch` in `src/syscall/syscall_dispatcher.cpp`.
3. Add unit tests in `tests/unit/` (and integration tests in `tests/integration/` if guest ELF execution is affected).

### Adding an NDK or Bionic Symbol
1. Implement the host thunk with `extern "C" __attribute__((sysv_abi))` in the appropriate subsystem under `src/ndk/` or its domain directory.
2. Register the symbol in `NdkSymbolRegistry` (`src/ndk/libandroid_thunks.cpp` or `RegisterBionicPosix` in `src/ndk/bionic_posix.cpp`).
3. Export a matching stub symbol in the corresponding guest stub library under `tests/guests/` (such as `tests/guests/libc.c` or `tests/guests/libandroid.c`) so guest test binaries and APKs can link against it.
4. Add unit tests verifying functionality and symbol lookup, plus guest-side exercise where applicable.

---

## Commit & Pull Request Guidelines

- **Commit Messages**: Use a single-line imperative header describing the change (for example, `Implement binary resource table parser and asset pipeline`). Do not use conventional commit prefixes (`feat:`, `fix:`) and do not include a commit body.
- **Focused Scope**: Keep each pull request scoped to a single logical feature, subsystem improvement, or bug fix.
- **Zero Regressions**: Ensure 100% of existing and newly added CTest targets pass before opening a pull request.

---

## License

Portage is licensed under the GNU General Public License v3.0. See [`LICENSE`](LICENSE) for details. By submitting a contribution, you agree that your work will be licensed under the same terms.
