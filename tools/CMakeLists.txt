add_executable(assemble_samples assemble_samples.cpp)
target_link_libraries(assemble_samples PRIVATE ZLIB::ZLIB)
set_target_properties(assemble_samples PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
