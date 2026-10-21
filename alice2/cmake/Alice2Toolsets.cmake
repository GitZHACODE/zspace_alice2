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
    set(ZSPACE_TOOLSETS_SLICING_WITH_OPENUSD ${ALICE2_WITH_TS3DP_USD} CACHE BOOL "Native slicer USD IO" FORCE)
    add_subdirectory("${ZSPACE_TOOLSETS_DIR}" "${CMAKE_BINARY_DIR}/zspace_toolsets_build")
endif()

function(alice2_deploy_toolsets_runtime target)
    if(NOT ALICE2_WITH_ZSPACE_CORE OR NOT ALICE2_WITH_TS3DP_USD)
        return()
    endif()
    # USD is implemented in toolsets; this flag only enables the sketch's IO UI.
    target_compile_definitions(${target} PRIVATE ZSPACE_TS3DP_OPENUSD)
    set(runtime_root "${ZSPACE_OPENUSD_ROOT}")
    if(ZSPACE_OPENUSD_RUNTIME_ROOT)
        set(runtime_root "${ZSPACE_OPENUSD_RUNTIME_ROOT}")
    endif()
    file(GLOB usd_dlls "${runtime_root}/bin/*.dll" "${runtime_root}/lib/*.dll")
    if(WIN32 AND NOT usd_dlls)
        message(FATAL_ERROR "No native USD runtime DLLs found in ${runtime_root}/bin or lib")
    endif()
    foreach(dll IN LISTS usd_dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
        install(FILES "${dll}" DESTINATION ${ALICE2_INSTALL_RUNTIME_DESTINATION})
    endforeach()
    if(NOT EXISTS "${runtime_root}/lib/usd/plugInfo.json")
        message(FATAL_ERROR "Missing OpenUSD schema registry: ${runtime_root}/lib/usd/plugInfo.json")
    endif()
    # OpenUSD searches "usd" relative to its DLL; keep resources beside copied DLLs.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${runtime_root}/lib/usd"
                "$<TARGET_FILE_DIR:${target}>/usd")
    install(DIRECTORY "${runtime_root}/lib/usd" DESTINATION ${ALICE2_INSTALL_RUNTIME_DESTINATION})
endfunction()