# Add or remove sketch names here (without .cpp). An empty list is valid.
set(tna_sketches
    sketch_tna
)

foreach(sketch IN LISTS tna_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_tna alice2_json)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_tna_examples SOURCES
#     sketch_tna.cpp
#     LIBRARIES alice2_tna alice2_json
# )
