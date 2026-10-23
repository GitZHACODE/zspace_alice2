if(NOT ALICE2_BUILD_MODE STREQUAL "test")
    alice2_add_sketch(alice2 DEFAULT SOURCES base/sketch_base.cpp)
endif()
alice2_add_sketch(sketch_base SOURCES base/sketch_base.cpp)

# Each category owns its app declarations. Add a subdirectory here for new work.
add_subdirectory(field)
add_subdirectory(geometry)
add_subdirectory(stack)
add_subdirectory(waveLatent)
add_subdirectory(minimal)
