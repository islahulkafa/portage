cmake_minimum_required(VERSION 3.25)
project(portage CXX C)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "Portage requires Clang (clang/clang++) with the Ninja generator. Visual Studio / cl.exe is not supported because Portage requires __attribute__((sysv_abi)), -mfsgsbase, and Android x86_64 guest cross-compilation.\n\nPlease configure using:\n  cmake --preset debug\nor specify:\n  cmake -B <dir> -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VULKAN_INCLUDE_DIR "")
if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include")
    set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
elseif(EXISTS "C:/Dev/VulkanSDK/1.4.363.0/Include")
    set(VULKAN_INCLUDE_DIR "C:/Dev/VulkanSDK/1.4.363.0/Include")
endif()

set(PORTAGE_ANDROID_NDK_COMPILER "")
set(PORTAGE_ANDROID_NDK_LIBCXX "")

file(GLOB NDK_CANDIDATE_DIRS "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/*")
list(REVERSE NDK_CANDIDATE_DIRS)

set(NDK_ROOT_CANDIDATES
    "$ENV{ANDROID_NDK_ROOT}"
    "$ENV{ANDROID_NDK_HOME}"
    ${NDK_CANDIDATE_DIRS}
)

foreach(ndk_dir ${NDK_ROOT_CANDIDATES})
    if(ndk_dir AND EXISTS "${ndk_dir}")
        file(GLOB_RECURSE NDK_CLANG_CANDIDATES "${ndk_dir}/x86_64-linux-android3*-clang++.cmd" "${ndk_dir}/x86_64-linux-android*-clang++")
        if(NDK_CLANG_CANDIDATES)
            list(GET NDK_CLANG_CANDIDATES 0 NDK_COMPILER_FOUND)
            set(LIBCXX_CANDIDATE "${ndk_dir}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so")
            if(NOT EXISTS "${LIBCXX_CANDIDATE}")
                file(GLOB_RECURSE LIBCXX_SEARCH "${ndk_dir}/*/x86_64-linux-android/libc++_shared.so")
                if(LIBCXX_SEARCH)
                    list(GET LIBCXX_SEARCH 0 LIBCXX_CANDIDATE)
                endif()
            endif()
            if(EXISTS "${LIBCXX_CANDIDATE}")
                set(PORTAGE_ANDROID_NDK_COMPILER "${NDK_COMPILER_FOUND}")
                set(PORTAGE_ANDROID_NDK_LIBCXX "${LIBCXX_CANDIDATE}")
                message(STATUS "Found Android NDK Clang++: ${PORTAGE_ANDROID_NDK_COMPILER}")
                message(STATUS "Found Android NDK libc++_shared.so: ${PORTAGE_ANDROID_NDK_LIBCXX}")
                break()
            endif()
        endif()
    endif()
endforeach()

include(cmake/CPM.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
