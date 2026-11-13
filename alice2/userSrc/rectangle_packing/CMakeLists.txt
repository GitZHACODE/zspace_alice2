alice2_add_sketch(sketch_rectangle_packing SOURCES sketch_rectangle_packing.cpp LIBRARIES alice2_json)
# Seed a portable input file without overwriting edits made beside the executable.
get_target_property(packing_runtime_dir sketch_rectangle_packing RUNTIME_OUTPUT_DIRECTORY)
add_custom_target(rectangle_packing_data
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/packing.json"
        "-DLEGACY=${packing_runtime_dir}/packing.json"
        "-DOUTPUT=${packing_runtime_dir}/data/packing.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/StagePackingData.cmake"
    VERBATIM)
add_dependencies(sketch_rectangle_packing rectangle_packing_data)
if(BUILD_TESTING)
    add_executable(rectangle_packing_checks EXCLUDE_FROM_ALL checks.cpp)
    target_link_libraries(rectangle_packing_checks PRIVATE alice2_json)
    target_compile_definitions(rectangle_packing_checks PRIVATE PACKING_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    alice2_runtime(rectangle_packing_checks checks)
    add_test(NAME rectangle_packing COMMAND rectangle_packing_checks)
endif()
