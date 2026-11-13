# CLI reference

This reference documents the exact `portage.exe` command syntax, launcher flags, host environment variables, and injected guest environment parsed in [main.cpp](../src/launcher/main.cpp).

## Synopsis

```powershell
portage.exe [--verbose] [--headless | --interactive] [--check-apk] [--location <lat>,<lon>] [--battery <level>[,<charging>]] [--rootfs <path>] [run] <elf_binary | app.apk> [args...]
portage.exe logcat
```

## Subcommands

| Subcommand | Arguments | Behavior |
| :--- | :--- | :--- |
| `run` | `<elf_binary \| app.apk> [args...]` | Optional explicit run verb (stripped during flag parsing; omitting `run` behaves identically). |
| `logcat` | None | Dumps buffered Android `liblog` entries to stdout and exits with `0`. |

## Command-line options

| Option | Argument Format | Default | Behavior |
| :--- | :--- | :--- | :--- |
| `--headless` | None | Disabled unless `PORTAGE_HEADLESS` is set | Runs APK lifecycle without creating or showing a Win32 window; drives `onStart` through `onDestroy` and exits automatically. |
| `--interactive` | None | Enabled for `.apk` targets | Forces interactive Win32 window mode (`1280x720`), blocking until the user closes the window. Overrides `PORTAGE_HEADLESS`. |
| `--check-apk` | None | Disabled | Audits an `.apk` target's native libraries and unresolved symbols against `NdkSymbolRegistry` without executing the app. |
| `--verbose` | None | Disabled | Enables syscall dispatch diagnostics, prints APK package/version metadata, and logs ELF load base/entry/interpreter/PIE status. |
| `--rootfs` | `<path>` | Empty (`PORTAGE_ROOTFS` if set) | Mounts host `<path>` as the guest root filesystem and places APK sandboxes under `<path>/sandbox`. |
| `--location` | `<lat>,<lon>` | Unset | Sets simulated GPS latitude and longitude in `LocationManager`. |
| `--battery` | `<level>[,<charging>]` | Full / charging | Overrides simulated battery percentage (`int32`) and optional charging state (`0` = discharging, non-`0` or omitted = charging). |

> [!NOTE]
> When running an `.apk` target without `--headless`, Portage attempts to create a `1280x720` Win32 window titled `Portage: <package_name>`. If window creation fails, execution falls back automatically to headless lifecycle dispatch.

## Host environment variables

| Variable | Recognized Values | Effect |
| :--- | :--- | :--- |
| `PORTAGE_HEADLESS` | Any non-empty string other than `"0"` | Enables headless mode by default (can still be overridden by passing `--interactive` on the CLI). |
| `PORTAGE_ROOTFS` | Host directory path | Supplies the default guest rootfs path when `--rootfs` is omitted. |

## Default guest environment

Every launched guest ELF binary receives the following default environment variables in its initial `SetupStack` block:

| Guest Variable | Value |
| :--- | :--- |
| `PATH` | `/system/bin:/system/xbin` |
| `ANDROID_DATA` | `/data` |
| `ANDROID_ROOT` | `/system` |
| `TMPDIR` | `/data/local/tmp` |

## APK vs standalone ELF execution path

1. **`.apk` / `.APK` targets**:
   - Extracts archive contents, parses `AndroidManifest.xml` and `resources.arsc`, and locates native libraries (`PackageManager`) as well as `classes*.dex` (`DexFile`).
   - Adds `/system/lib64`, the extracted APK `lib/` directory, and the library's parent directory to [`LinkMap`](../src/elf/link_map.hpp).
   - Routes `NativeActivity` targets through `JNI_OnLoad(JavaVM *, nullptr)` and `ANativeActivity_onCreate`; routes pure-DEX and hybrid Java-native APKs through [`JavaActivityRunner`](../src/package/java_activity.hpp) on [`DexVm`](../src/package/dex_vm.hpp) (`Application.onCreate`, `Activity` lifecycle, `System.loadLibrary` + `JNI_OnLoad`, and `SurfaceView`/`ANativeWindow_fromSurface`) before calling `LinkMap::FinalizeAll()`.
2. **Standalone ELF targets**:
   - Configures the Linux initial stack (`argc`, `argv`, `envp`, `auxv`), transfers control to `loaded_elf.entry_point` via `GuestContext::Run`, prints `[portage] Guest exited with code: <code>`, finalizes destructors, and exits with the guest return code.
