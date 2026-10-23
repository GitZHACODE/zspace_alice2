# Add or remove sketch names here (without .cpp). An empty list is valid.
set(minimal_sketches
    sketch_minimal
)

foreach(sketch IN LISTS minimal_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp Orbit.cpp)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_minimal_examples SOURCES
#     sketch_minimal.cpp Orbit.cpp
# )
