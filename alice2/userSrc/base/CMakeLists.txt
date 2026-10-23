# Add or remove sketch names here (without .cpp). An empty list is valid.
set(base_sketches
    sketch_base
)

foreach(sketch IN LISTS base_sketches)
    alice2_add_sketch(${sketch} DEFAULT SOURCES ${sketch}.cpp)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_base_examples SOURCES
#     sketch_base.cpp
# )
