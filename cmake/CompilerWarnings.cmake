# cmake/CompilerWarnings.cmake
add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    -mfsgsbase
    -Wall
    -Wextra
    -Wpedantic
    -Wno-language-extension-token
)


