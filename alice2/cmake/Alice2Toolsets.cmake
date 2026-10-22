# Toolsets owns compilation and optional native dependencies. Its public target
# propagates headers and DLL import definitions to the desktop client.
option(ALICE2_BUILD_SLICING_TESTS "Build native desktop slicing integration checks" OFF)
if(ALICE2_BUILD_SLICING_TESTS)
    include(CTest)
endif()

option(ALICE2_WITH_TS3DP_USD "Enable native OpenUSD slicer input/export" ON)
if(ALICE2_WITH_ZSPACE_CORE)
    if(NOT EXISTS "${ZSPACE_TOOLSETS_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${ZSPACE_TOOLSETS_DIR}/include/zspace/slicing.h")
        message(FATAL_ERROR "ZSPACE_TOOLSETS_DIR must point to modern toolsets with include/zspace/slicing.h")
    endif()
    option(ZSPACE_TOOLSETS_WITH_TETGEN "Enable desktop harmonic slicing" ON)
    option(ZSPACE_TOOLSETS_SLICING_OPENMP "Enable desktop slicer parallelism" ON)
    add_subdirectory("${ZSPACE_TOOLSETS_DIR}" "${CMAKE_BINARY_DIR}/zspace_toolsets_build")
endif()

function(alice2_deploy_toolsets_runtime target)
    if(NOT ALICE2_WITH_ZSPACE_CORE OR NOT ALICE2_WITH_TS3DP_USD)
        return()
    endif()
    # USD is implemented in Core IO; this flag only enables the sketch's IO UI.
    target_compile_definitions(${target} PRIVATE ALICE2_SLICER_USD)
    zspace_deploy_runtime(TARGET ${target} COMPONENTS io
        INSTALL_DESTINATION "${ALICE2_INSTALL_RUNTIME_DESTINATION}")
endfunction()
