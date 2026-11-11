# cmake/CompilerWarnings.cmake
add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    -mfsgsbase
    -Wall
    -Wextra
    -Wpedantic
    -Wno-language-extension-token
)

target_compile_definitions(project_warnings INTERFACE
    _AMD64_
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)

