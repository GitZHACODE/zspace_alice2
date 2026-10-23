# Add or remove sketch names here (without .cpp). An empty list is valid.
set(planarise_sketches
    sketch_planarise
    sketch_circular_conical
)

foreach(sketch IN LISTS planarise_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_projection alice2_analysis)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_planarise_examples SOURCES
#     sketch_planarise.cpp
#     LIBRARIES alice2_projection alice2_analysis
# )
