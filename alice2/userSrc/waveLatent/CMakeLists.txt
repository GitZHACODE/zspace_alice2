# Add or remove sketch names here (without .cpp). An empty list is valid.
set(waveLatent_sketches
    sketch_waveLatent
    sketch_waveLatent_stack
)

foreach(sketch IN LISTS waveLatent_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp LIBRARIES alice2_wave_latent)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_waveLatent_examples SOURCES
#     sketch_waveLatent.cpp
#     LIBRARIES alice2_wave_latent
# )
