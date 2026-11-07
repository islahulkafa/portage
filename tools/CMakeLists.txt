add_executable(assemble_samples assemble_samples.cpp)
set_target_properties(assemble_samples PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
