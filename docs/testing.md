# Verify and test Portage

Run the 5-gate verification pipeline, filter unit and integration suites, cross-compile Android `x86_64` guest test binaries, and assemble synthetic APKs.
All commands use the Ninja `debug` preset configured in [CMakePresets.json](../CMakePresets.json).

## Run the 5-gate verification pipeline

Execute all five gates sequentially after modifying runtime, linker, or guest code:

1. **Compile with zero warnings or errors**:

   ```powershell
   cmake --build build/debug
   ```

2. **Run the targeted GoogleTest unit suite**:

   ```powershell
   .\build\debug\tests\unit_tests.exe --gtest_filter=LinkerExtensionsTest.*
   ```

3. **Run the targeted GoogleTest integration suite**:

   ```powershell
   .\build\debug\tests\integration_tests.exe --gtest_filter=BionicRuntimeTest.*
   ```

4. **Run all registered CTest cases** (`584` total: `544` unit tests and `40` integration tests):

   ```powershell
   ctest --test-dir build/debug --output-on-failure
   ```

5. **Verify headless APK execution via the launcher**:

   ```powershell
   .\build\debug\src\portage.exe --headless run build\debug\game_engine.apk
   ```

   Look for `[portage] APK native activity execution completed successfully` and exit code `0`.

## Add guest test binaries and stubs

Cross-compiled Android `x86_64` fixtures live under [tests/guests/](../tests/guests/CMakeLists.txt) and compile automatically during `cmake --build build/debug`.

- **Guest compilation target flags**: `tests/guests/CMakeLists.txt` invokes Clang with `--target=x86_64-linux-android -fuse-ld=lld -nostdlib` alongside `-fPIE "-Wl,-pie"` (for executables) or `-shared -fPIC` (for stub libraries placed in `rootfs/system/lib64/`).
- **Testing native GNU2 TLS descriptors**: Clang defaults to `-femulated-tls` on `x86_64-linux-android`. Pass `-fno-emulated-tls -mtls-dialect=gnu2` if compiling guest C fixtures to emit `R_X86_64_TLSDESC` relocations instead of `__emutls_get_address`.

> [!NOTE]
> Register new C++ GoogleTest files in `UNIT_TEST_SOURCES` or `INTEGRATION_TEST_SOURCES` inside [tests/CMakeLists.txt](../tests/CMakeLists.txt), and register any new guest cross-compilation outputs in the `build_guest_binaries` target inside [tests/guests/CMakeLists.txt](../tests/guests/CMakeLists.txt).

## Assemble sample APKs and OBBs

[assemble_samples.cpp](../tools/assemble_samples.cpp) builds uncompressed/compressed test APKs containing binary `AndroidManifest.xml`, `resources.arsc`, assets, native `.so` libraries, and companion `.obb` expansion archives:

```powershell
.\build\debug\tools\assemble_samples.exe output.apk com.example.app native_lib path\to\libnative_lib.so
```

This generates `output.apk` alongside `main.1.com.example.app.obb` in the same output directory.
