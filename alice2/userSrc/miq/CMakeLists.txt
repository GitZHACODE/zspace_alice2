# Add or remove sketch names here (without .cpp). An empty list is valid.
set(miq_sketches
    sketch_miq_curvature_remesh
    sketch_miq_stress_remesh
    sketch_stress_aligned_streamlines
)

if(ALICE2_ENABLE_MIQ_REMESH)
    foreach(sketch IN LISTS miq_sketches)
        alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_remeshing alice2_analysis)
    endforeach()
endif()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_miq_examples SOURCES
#     sketch_miq_curvature_remesh.cpp
#     LIBRARIES alice2_remeshing alice2_analysis
# )
