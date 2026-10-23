set(field_sketches
    sketch_scalarFieldSimple
    sketch_scalarField_01_basic
    sketch_scalarField_02_boolean
    sketch_scalarField_03_blending
    sketch_scalarField_04_directional
    sketch_scalarField_05_towerSet
    sketch_sdf_basic)
# sketch_voronoi.cpp is a reference using the retired ContourData API.
foreach(sketch IN LISTS field_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_fields)
endforeach()

# Optional collection: only these linked sketches appear in its switcher.
alice2_add_sketch(sketch_field_examples SOURCES
    sketch_scalarField_01_basic.cpp sketch_scalarField_02_boolean.cpp
    sketch_scalarField_03_blending.cpp
    LIBRARIES alice2_fields)
