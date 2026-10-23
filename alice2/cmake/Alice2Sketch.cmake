# SOURCES may contain one sketch, a collection, and any experiment-local classes.
# Keep sketch sources on the executable so static registration is not discarded.
function(alice2_add_sketch target)
    cmake_parse_arguments(APP "DEFAULT" "" "SOURCES;LIBRARIES" ${ARGN})
    if(APP_UNPARSED_ARGUMENTS OR NOT APP_SOURCES)
        message(FATAL_ERROR "alice2_add_sketch(${target}): expected SOURCES and optional LIBRARIES")
    endif()
    if(APP_DEFAULT)
        add_executable(${target} ${APP_SOURCES} $<TARGET_OBJECTS:alice2_entry>)
    else()
        add_executable(${target} EXCLUDE_FROM_ALL ${APP_SOURCES} $<TARGET_OBJECTS:alice2_entry>)
    endif()
    target_link_libraries(${target} PRIVATE alice2_viewer ${APP_LIBRARIES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${target} PROPERTIES
        FOLDER "Sketches"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    if(WIN32 AND BUILD_SHARED_LIBS AND EXISTS "${GLFW_DIR}/bin/glfw3.dll")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLFW_DIR}/bin/glfw3.dll" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endif()
    if(WIN32 AND ALICE2_ENABLE_MIQ_REMESH AND TARGET CoMISo)
        get_target_property(comiso_dir CoMISo SOURCE_DIR)
        file(GLOB blas_dlls "${comiso_dir}/ext/OpenBLAS-v0.2.14-Win64-int64/bin/*.dll")
        if(blas_dlls)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${blas_dlls} "$<TARGET_FILE_DIR:${target}>" VERBATIM)
        endif()
    endif()
    if(APP_DEFAULT)
        install(TARGETS ${target} RUNTIME DESTINATION bin)
    else()
        install(TARGETS ${target} RUNTIME DESTINATION bin COMPONENT ${target} EXCLUDE_FROM_ALL)
    endif()
endfunction()
