# Add or remove sketch names here (without .cpp). An empty list is valid.
set(field_sketches
    sketch_scalarFieldSimple
    sketch_scalarField_01_basic
    sketch_scalarField_02_boolean
    sketch_scalarField_03_blending
    sketch_scalarField_04_directional
    sketch_scalarField_05_towerSet
    sketch_sdf_basic
    # sketch_voronoi uses retired APIs and is retained as a reference only.
)

foreach(sketch IN LISTS field_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_fields)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_field_examples SOURCES
#     sketch_scalarFieldSimple.cpp
#     LIBRARIES alice2_fields
# )
