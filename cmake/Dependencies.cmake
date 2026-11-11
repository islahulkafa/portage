set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_LIBRARIES ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_HEADERS ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_FILES ON CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME zlib
    VERSION 1.3.1
    URL "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "ZLIB_BUILD_EXAMPLES OFF"
        "SKIP_INSTALL_ALL ON"
        "SKIP_INSTALL_LIBRARIES ON"
        "SKIP_INSTALL_HEADERS ON"
        "SKIP_INSTALL_FILES ON"
)

foreach(zlib_target IN ITEMS zlib zlibstatic)
    if(TARGET ${zlib_target})
        target_compile_definitions(${zlib_target} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    endif()
endforeach()

if(TARGET zlibstatic)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
elseif(TARGET zlib)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
endif()

# gtest - Google Testing and Mocking Framework
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME googletest
    VERSION 1.14.0
    URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "INSTALL_GTEST OFF"
)

if(TARGET gtest AND NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
endif()
if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
endif()

foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
    if(TARGET ${gtest_target})
        set_target_properties(${gtest_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion -Wno-undef)
        endif()
    endif()
endforeach()
