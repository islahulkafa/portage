#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "location/location_manager.hpp"
#include "ndk/android_log.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "power/power_manager.hpp"
#include "package/dex_vm.hpp"
#include "package/java_activity.hpp"
#include "package/jni_env.hpp"
#include "package/native_activity.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr
            << "Usage: portage [--verbose] [--headless] [--check-apk] [--location <lat>,<lon>] "
               "[--battery <level>,<charging>] [--rootfs <path>] <elf_binary | app.apk> [args...]\n"
            << "       portage run <app.apk>\n"
            << "       portage logcat\n";
        return 1;
    }

    if (argc >= 2 && std::string(argv[1]) == "logcat") {
        portage::ndk::LogcatBuffer::Instance().DumpToStream(std::cout);
        return 0;
    }

    bool verbose = false;
    bool headless = false;
    bool check_apk = false;
    if (const char *env_hl = std::getenv("PORTAGE_HEADLESS"); env_hl && *env_hl != '0') {
        headless = true;
    }
    std::filesystem::path rootfs_path{};
    std::string target_path{};
    std::vector<std::string> guest_args{};

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "run") {
            continue;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--check-apk") {
            check_apk = true;
            headless = true;
        } else if (arg == "--interactive") {
            headless = false;
        } else if (arg == "--location") {
            if (i + 1 >= argc) {
                std::cerr << "Error: missing coordinates after --location\n";
                return 1;
            }
            std::string loc_str = argv[++i];
            auto comma = loc_str.find(',');
            if (comma != std::string::npos) {
                double lat = std::stod(loc_str.substr(0, comma));
                double lon = std::stod(loc_str.substr(comma + 1));
                portage::location::LocationManager::Instance().SetLocation(lat, lon);
            }
        } else if (arg == "--battery") {
            if (i + 1 >= argc) {
                std::cerr << "Error: missing parameters after --battery\n";
                return 1;
            }
            std::string bat_str = argv[++i];
            auto comma = bat_str.find(',');
            if (comma != std::string::npos) {
                int32_t level = std::stoi(bat_str.substr(0, comma));
                bool charging = (std::stoi(bat_str.substr(comma + 1)) != 0);
                portage::power::PowerManager::Instance().SetSimulatedBattery(level, charging);
            } else {
                int32_t level = std::stoi(bat_str);
                portage::power::PowerManager::Instance().SetSimulatedBattery(level, true);
            }
        } else if (arg == "--rootfs") {
            if (i + 1 >= argc) {
                std::cerr << "Error: missing path after --rootfs\n";
                return 1;
            }
            rootfs_path = argv[++i];
        } else if (target_path.empty()) {
            target_path = arg;
            guest_args.push_back(arg);
        } else {
            guest_args.push_back(arg);
        }
    }

    if (target_path.empty()) {
        std::cerr << "Error: missing target binary or APK path\n";
        return 1;
    }

    if (rootfs_path.empty()) {
        const char *env_rootfs = std::getenv("PORTAGE_ROOTFS");
        if (env_rootfs && *env_rootfs) {
            rootfs_path = env_rootfs;
        }
    }
    bool is_apk = (target_path.size() >= 4 &&
                   (target_path.ends_with(".apk") || target_path.ends_with(".APK")));

    ANativeWindow default_window{1280, 720, 1, 1, nullptr};
    std::string elf_path = target_path;
    bool has_primary_lib = true;
    bool has_dex_launcher = false;
    std::string launcher_cls_slash;
    portage::thread::ThreadManager::Instance().EnsureCurrentThreadTcb();

    if (is_apk) {
        portage::package::PackageManager::Instance().Initialize(
            rootfs_path.empty() ? "" : (rootfs_path / "sandbox"));
        auto pkg_opt = portage::package::PackageManager::Instance().InstallPackage(target_path);
        if (!pkg_opt.has_value()) {
            std::cerr << "Error: failed to install or parse APK: " << target_path << "\n";
            return 1;
        }
        const auto &pkg = *pkg_opt;
        if (verbose) {
            std::cout << "[portage] Installed APK: " << pkg.manifest.package_name << " (v"
                      << pkg.manifest.version_code << ")\n";
        }
        auto primary_lib = portage::package::PackageManager::Instance().GetPrimaryLibraryPath(pkg);
        has_primary_lib = std::filesystem::exists(primary_lib);
        std::string launcher_cls = pkg.manifest.launcher_activity_name.empty()
                                       ? pkg.manifest.main_activity_name
                                       : pkg.manifest.launcher_activity_name;
        launcher_cls_slash = launcher_cls;
        for (char &c : launcher_cls_slash) {
            if (c == '.') {
                c = '/';
            }
        }
        has_dex_launcher = !launcher_cls_slash.empty() &&
                           portage::package::DexVm::Instance().HasClass(launcher_cls_slash);

        if (!has_primary_lib && !has_dex_launcher) {
            std::cerr << "Error: native library not found: " << primary_lib.string() << "\n";
            return 1;
        }
        if (has_primary_lib) {
            elf_path = primary_lib.string();
        } else {
            elf_path = (pkg.lib_dir / "libportage_dex_placeholder.so").string();
        }

        if (!headless) {
            std::string win_title = "Portage: " + pkg.manifest.package_name;
            if (portage::wsi::WindowManager::Instance().EnsureWindow(1280, 720, win_title)) {
                default_window.native_handle =
                    portage::wsi::WindowManager::Instance().GetWindowHandle();
                default_window.width =
                    static_cast<int32_t>(portage::wsi::WindowManager::Instance().GetWidth());
                default_window.height =
                    static_cast<int32_t>(portage::wsi::WindowManager::Instance().GetHeight());
            } else {
                headless = true;
            }
        }

        portage::package::NativeActivityRunner::Instance().SetupActivity(pkg, &default_window);
        portage::package::JavaActivityRunner::Instance().Setup(pkg, &default_window);
    }

    std::vector<std::string> guest_env = {"PATH=/system/bin:/system/xbin", "ANDROID_DATA=/data",
                                          "ANDROID_ROOT=/system", "TMPDIR=/data/local/tmp"};

    portage::fs::Vfs vfs(rootfs_path);
    vfs.SetExecutablePath(std::filesystem::absolute(elf_path));

    portage::fs::FileDescriptorTable fd_table;
    portage::thread::ThreadManager::Instance().RegisterMainThread();

    portage::mem::AddressSpace address_space;
    portage::syscall::SyscallDispatcher::Initialize(&address_space, &vfs, &fd_table, verbose);

    portage::elf::LinkMap::Instance().Reset();
    portage::elf::LinkMap::Instance().SetAddressSpace(&address_space);
    portage::elf::LinkMap::Instance().SetVfs(&vfs);
    portage::elf::LinkMap::Instance().AddSearchPath("/system/lib64");
    portage::thread::ThreadManager::Instance().EnsureCurrentThreadTcb();

    if (is_apk) {
        const auto *pkg_ptr = portage::package::PackageManager::Instance().GetActivePackage();
        if (pkg_ptr) {
            portage::elf::LinkMap::Instance().AddSearchPath(pkg_ptr->lib_dir.string());
        }
    }
    portage::elf::LinkMap::Instance().AddSearchPath(
        std::filesystem::path(elf_path).parent_path().string());

    if (is_apk && !has_primary_lib) {
        if (check_apk) {
            const auto *pkg_ptr = portage::package::PackageManager::Instance().GetActivePackage();
            if (pkg_ptr) {
                std::cout << "[check-apk] Package: " << pkg_ptr->manifest.package_name
                          << " (targetSdk=" << pkg_ptr->manifest.target_sdk_version << ")\n";
            }
            std::cout << "[check-apk] Primary ELF: (pure/hybrid DEX launcher: "
                      << launcher_cls_slash << ")\n";
            std::cout << "[check-apk] Unresolved strong symbols: 0\n";
            portage::elf::LinkMap::Instance().FinalizeAll();
            portage::syscall::SyscallDispatcher::Shutdown();
            return 0;
        }
        bool ok = portage::package::JavaActivityRunner::Instance().LaunchAndRun(headless);
        std::cout << "[portage] APK Java activity execution "
                  << (ok ? "completed successfully" : "failed") << "\n";
        portage::elf::LinkMap::Instance().FinalizeAll();
        portage::syscall::SyscallDispatcher::Shutdown();
        return ok ? 0 : 1;
    }

    portage::elf::ElfLoader loader(&address_space);
    auto load_result = loader.LoadFromFile(
        elf_path, &vfs,
        [](std::string_view sym) {
            return portage::elf::LinkMap::Instance().ResolveGlobalSymbol(sym);
        },
        [&](const std::vector<std::string> &needed) {
            for (const auto &dep : needed) {
                (void)portage::elf::LinkMap::Instance().DlOpen(dep.c_str(), portage::elf::RTLD_NOW);
            }
        });

    if (std::holds_alternative<portage::elf::LoaderError>(load_result)) {
        auto err = std::get<portage::elf::LoaderError>(load_result);
        std::cerr << "Error: failed to load ELF binary: code=" << static_cast<int>(err) << "\n";
        return 1;
    }

    const auto &loaded_elf = std::get<portage::elf::LoadedElf>(load_result);
    portage::elf::LinkMap::Instance().RegisterLoadedElf(loaded_elf, elf_path);
    if (check_apk) {
        const auto *pkg_ptr = portage::package::PackageManager::Instance().GetActivePackage();
        if (pkg_ptr) {
            std::cout << "[check-apk] Package: " << pkg_ptr->manifest.package_name
                      << " (targetSdk=" << pkg_ptr->manifest.target_sdk_version << ")\n";
        }
        std::cout << "[check-apk] Primary ELF: "
                  << std::filesystem::path(elf_path).filename().string() << "\n";
        for (const auto &dep : loaded_elf.needed_libraries) {
            auto mod = portage::elf::LinkMap::Instance().FindModule(dep);
            if (!mod) {
                std::cout << "  DT_NEEDED " << dep << ": [MISSING]\n";
            } else if (mod->is_virtual) {
                std::cout << "  DT_NEEDED " << dep << ": [VIRTUAL]\n";
            } else {
                std::cout << "  DT_NEEDED " << dep << ": [LOADED] " << mod->filepath << "\n";
            }
        }
        auto unresolved = portage::elf::LinkMap::Instance().CollectUnresolvedSymbols();
        std::cout << "[check-apk] Unresolved strong symbols: " << unresolved.size() << "\n";
        for (const auto &sym : unresolved) {
            std::cout << "  - " << sym << "\n";
        }
        portage::elf::LinkMap::Instance().FinalizeAll();
        portage::syscall::SyscallDispatcher::Shutdown();
        return unresolved.empty() ? 0 : 1;
    }
    if (verbose) {
        std::cout << "[portage] Loaded ELF base: 0x" << std::hex << loaded_elf.base_address
                  << " entry: 0x" << loaded_elf.entry_point << " app_entry: 0x"
                  << loaded_elf.app_entry_point << " interp: " << loaded_elf.interpreter_path
                  << " PIE: " << (loaded_elf.is_pie ? "yes" : "no") << std::dec << "\n";
    }

    portage::arch::GuestContext guest_context;
    if (!guest_context.InitializeStack()) {
        std::cerr << "Error: failed to allocate guest stack\n";
        return 1;
    }

    portage::thread::ThreadManager::Instance().EnsureCurrentThreadTcb();

    if (is_apk) {
        uintptr_t jni_onload_addr = loaded_elf.FindSymbol("JNI_OnLoad");
        if (jni_onload_addr != 0) {
            auto jni_onload = reinterpret_cast<portage::package::JniOnLoadFunc>(jni_onload_addr);
            portage::package::jint ver =
                jni_onload(portage::package::JniEnvironment::Instance().GetJavaVM(), nullptr);
            if (verbose) {
                std::cout << "[portage] JNI_OnLoad returned version: 0x" << std::hex << ver
                          << std::dec << "\n";
            }
        }

        uintptr_t on_create_addr = loaded_elf.FindSymbol("ANativeActivity_onCreate");
        if (on_create_addr != 0 &&
            (!has_dex_launcher || launcher_cls_slash == "android/app/NativeActivity")) {
            auto on_create =
                reinterpret_cast<portage::package::ANativeActivity_createFunc>(on_create_addr);
            auto &runner = portage::package::NativeActivityRunner::Instance();
            on_create(runner.GetActivity(), nullptr, 0);
            runner.DispatchStart();
            runner.DispatchResume();
            runner.DispatchWindowCreated(&default_window);
            runner.DispatchInputQueueCreated(
                reinterpret_cast<AInputQueue *>(&runner.GetInputQueue()));
            runner.DispatchWindowFocus(true);

            if (!headless) {
                runner.ConnectInputQueueToWindow();
                if (verbose) {
                    std::cout << "[portage] Interactive window opened. Close window to exit.\n";
                }
                portage::wsi::WindowManager::Instance().WaitUntilClosed();
                portage::wsi::WindowManager::Instance().DestroyWindow();
                portage::wsi::WindowManager::Instance().ClearCallbacks();
            } else {
                runner.DispatchPause();
                runner.DispatchWindowDestroyed(&default_window);
                runner.DispatchInputQueueDestroyed(
                    reinterpret_cast<AInputQueue *>(&runner.GetInputQueue()));
                runner.DispatchStop();
                runner.DispatchDestroy();
            }

            std::cout << "[portage] APK native activity execution completed successfully\n";
            portage::elf::LinkMap::Instance().FinalizeAll();
            portage::syscall::SyscallDispatcher::Shutdown();
            return 0;
        }
        if (has_dex_launcher) {
            bool ok = portage::package::JavaActivityRunner::Instance().LaunchAndRun(headless);
            std::cout << "[portage] APK Java activity execution "
                      << (ok ? "completed successfully" : "failed") << "\n";
            portage::elf::LinkMap::Instance().FinalizeAll();
            portage::syscall::SyscallDispatcher::Shutdown();
            return ok ? 0 : 1;
        }
    }

    void *guest_sp = guest_context.SetupStack(guest_args, guest_env, loaded_elf.auxv_entries);
    if (!guest_sp) {
        std::cerr << "Error: failed to setup guest stack frame\n";
        return 1;
    }

    int exit_code = guest_context.Run(reinterpret_cast<void *>(loaded_elf.entry_point), guest_sp);
    std::cout << "[portage] Guest exited with code: " << exit_code << "\n";

    portage::elf::LinkMap::Instance().FinalizeAll();
    portage::syscall::SyscallDispatcher::Shutdown();
    return exit_code;
}
