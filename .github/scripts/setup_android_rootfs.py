#!/usr/bin/env python3
"""
Portage - Android Rootfs Setup Utility
Prepares the directory layout and populates Android system libraries from Android NDK
or an existing sysroot.
"""

import sys
import os
import shutil
import argparse
from pathlib import Path

def setup_rootfs(output_dir: Path, ndk_dir: Path = None):
    system_bin = output_dir / "system" / "bin"
    system_lib64 = output_dir / "system" / "lib64"
    vendor_lib64 = output_dir / "vendor" / "lib64"
    data_dir = output_dir / "data" / "local" / "tmp"

    for d in [system_bin, system_lib64, vendor_lib64, data_dir]:
        d.mkdir(parents=True, exist_ok=True)
    print(f"[+] Created Portage rootfs layout in: {output_dir}")

    if not ndk_dir:
        # Check standard SDK NDK path
        local_app_data = os.environ.get("LOCALAPPDATA", "")
        if local_app_data:
            candidate_ndk = Path(local_app_data) / "Android" / "Sdk" / "ndk"
            if candidate_ndk.exists():
                versions = sorted(candidate_ndk.glob("*"), reverse=True)
                if versions:
                    ndk_dir = versions[0]
                    print(f"[*] Detected Android NDK at: {ndk_dir}")

    if ndk_dir and ndk_dir.exists():
        # Look for x86_64 sysroot in NDK
        sysroot_lib = ndk_dir / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "sysroot" / "usr" / "lib" / "x86_64-linux-android"
        if sysroot_lib.exists():
            api_dirs = [d for d in sysroot_lib.glob("*") if d.is_dir() and d.name.isdigit()]
            if api_dirs:
                latest_api = sorted(api_dirs, key=lambda d: int(d.name), reverse=True)[0]
                print(f"[*] Sourcing NDK libraries from API level: {latest_api.name}")
                for lib_name in ["libc.so", "libm.so", "libdl.so", "liblog.so"]:
                    src_file = latest_api / lib_name
                    if src_file.exists():
                        shutil.copy2(src_file, system_lib64 / lib_name)
                        print(f"    Copied {lib_name} -> {system_lib64}")

    print("[+] Rootfs setup completed successfully.")

def main():
    parser = argparse.ArgumentParser(description="Setup Portage Android rootfs")
    parser.add_argument("--output", "-o", type=Path, default=Path("rootfs"), help="Target rootfs path")
    parser.add_argument("--ndk", type=Path, default=None, help="Android NDK root directory")
    args = parser.parse_args()

    setup_rootfs(args.output, args.ndk)

if __name__ == "__main__":
    main()
