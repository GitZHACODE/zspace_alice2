# Add or remove sketch names here (without .cpp). An empty list is valid.
set(stack_sketches
    sketch_ruled_surface_stacking
    sketch_stereotomy_stacking
)

foreach(sketch IN LISTS stack_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_stacking alice2_stereotomy)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_stack_examples SOURCES
#     sketch_ruled_surface_stacking.cpp
#     LIBRARIES alice2_stacking alice2_stereotomy
# )
