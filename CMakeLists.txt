cmake_minimum_required(VERSION 3.25)
project(portage CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VULKAN_INCLUDE_DIR "")
if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include")
    set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
elseif(EXISTS "C:/Dev/VulkanSDK/1.4.363.0/Include")
    set(VULKAN_INCLUDE_DIR "C:/Dev/VulkanSDK/1.4.363.0/Include")
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
