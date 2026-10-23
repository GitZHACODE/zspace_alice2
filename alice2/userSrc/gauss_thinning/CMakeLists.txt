# Add or remove sketch names here (without .cpp). An empty list is valid.
set(gauss_thinning_sketches
    sketch_gauss_thinning
)

foreach(sketch IN LISTS gauss_thinning_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_igl)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_gauss_thinning_examples SOURCES
#     sketch_gauss_thinning.cpp
#     LIBRARIES alice2_igl
# )
