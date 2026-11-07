# cmake/Dependencies.cmake
if(NOT ZLIB_ROOT AND EXISTS "C:/Dev/msys64/ucrt64")
    set(ZLIB_ROOT "C:/Dev/msys64/ucrt64")
endif()
find_package(ZLIB REQUIRED)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
endif()
