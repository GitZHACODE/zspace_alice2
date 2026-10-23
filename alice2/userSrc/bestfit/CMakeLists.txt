# Add or remove sketch names here (without .cpp). An empty list is valid.
set(bestfit_sketches
    sketch_bestfit_tna_v2
    # sketch_bestfit_tna
    # sketch_projected_mixed_fdm
    # sketch_tna_offset_bestfit
)

foreach(sketch IN LISTS bestfit_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_tna alice2_json)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_bestfit_examples SOURCES
#     sketch_bestfit_tna_v2.cpp
#     LIBRARIES alice2_tna alice2_json
# )
