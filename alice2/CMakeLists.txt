cmake_minimum_required(VERSION 3.16)

project(alice2 VERSION 3.0.0 LANGUAGES C CXX)

option(ALICE2_ENABLE_CUDA "Enable CUDA build of alice2" OFF)
option(ALICE2_WITH_ZSPACE_CORE "Enable local zspace_core integration" ON)
set(ZSPACE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../zspace_core" CACHE PATH "Path to local zspace_core checkout")
set(ALICE2_BUILD_MODE "default" CACHE STRING "Select alice2 build mode (default or test)")
set_property(CACHE ALICE2_BUILD_MODE PROPERTY STRINGS default test)
set(ALICE2_USING_TEST_MODE OFF)
if(ALICE2_BUILD_MODE STREQUAL "test")
    set(ALICE2_USING_TEST_MODE ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ALICE2_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75)
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)

# Fallback to SDK if local source does not exist
if(ALICE2_WITH_ZSPACE_CORE AND NOT EXISTS "${ZSPACE_CORE_DIR}/CMakeLists.txt")
    message(STATUS "ALICE2_WITH_ZSPACE_CORE=ON but local zspace_core was not found at: ${ZSPACE_CORE_DIR}. Falling back to precompiled SDK.")
    set(ALICE2_WITH_ZSPACE_CORE OFF)
endif()

set(ALICE2_USING_ZSPACE_SDK OFF)
if(NOT ALICE2_WITH_ZSPACE_CORE)
    set(ZSPACE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/depends/zspace")
    # Download and extract SDK if not present locally
    if(NOT EXISTS "${ZSPACE_SDK_DIR}/include/zspace/interface.h")
        set(ZSPACE_URL "https://github.com/GitZHACODE/zspace_alice2/releases/download/depends/zspace.zip")
        message(STATUS "Downloading precompiled zSpace SDK from ${ZSPACE_URL}...")
        file(DOWNLOAD "${ZSPACE_URL}" "${CMAKE_CURRENT_SOURCE_DIR}/depends/zspace.zip" SHOW_PROGRESS STATUS dl_status)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            message(WARNING "Failed to download zSpace SDK: ${dl_status}. If you have local zspace_core source, enable ALICE2_WITH_ZSPACE_CORE.")
        else()
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_CURRENT_SOURCE_DIR}/depends/zspace.zip"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/depends"
                RESULT_VARIABLE unpack_ret
            )
            if(NOT unpack_ret EQUAL 0)
                message(FATAL_ERROR "Failed to unpack zspace.zip")
            endif()
            file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/depends/zspace.zip")
            message(STATUS "Extracted and removed zspace.zip")
        endif()
    endif()

    if(EXISTS "${ZSPACE_SDK_DIR}/include/zspace/interface.h")
        set(ALICE2_USING_ZSPACE_SDK ON)
    else()
        message(FATAL_ERROR "zspace_core source not found and precompiled zSpace SDK is missing. Please enable ALICE2_WITH_ZSPACE_CORE or check internet connection.")
    endif()
endif()

if(ALICE2_WITH_ZSPACE_CORE)
    set(ZSPACE_BUILD_INTERFACE ON CACHE BOOL "Build zSpace interface DLL" FORCE)
    set(ZSPACE_BUILD_IO ON CACHE BOOL "Build zSpace IO DLL" FORCE)
    set(ZSPACE_BUILD_DISPLAY OFF CACHE BOOL "Build zSpace display DLL" FORCE)
    set(ZSPACE_BUILD_INTEROP OFF CACHE BOOL "Build zSpace interop DLL" FORCE)
    set(ZSPACE_BUILD_TESTS OFF CACHE BOOL "Build zSpace tests" FORCE)
    set(ZSPACE_BUILD_SHARED ON CACHE BOOL "Build zSpace DLLs" FORCE)
    set(ZSPACE_WITH_OPENGL OFF CACHE BOOL "Disable zSpace display OpenGL for alice2 integration" FORCE)

    add_subdirectory("${ZSPACE_CORE_DIR}" "${CMAKE_BINARY_DIR}/zspace_core_build")

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_deps/CMakeTargets")

find_package(OpenGL REQUIRED)
if(ALICE2_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()

set(ALICE2_DEPENDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/depends")
file(GLOB DEP_FILES "${ALICE2_DEPENDS_DIR}/*")
if(NOT DEP_FILES)
    file(MAKE_DIRECTORY "${ALICE2_DEPENDS_DIR}")

    set(ASSET_URLS
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/eigen.zip"
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/glew.zip"
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/glfw.zip"
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/gl2ps-1.4.2.zip"
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/nlohmann.zip"
        "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/stb.zip"
    )

    foreach(url IN LISTS ASSET_URLS)
        get_filename_component(name "${url}" NAME)
        message(STATUS "Downloading ${name} from ${url}…")
        file(DOWNLOAD "${url}" "${ALICE2_DEPENDS_DIR}/${name}" SHOW_PROGRESS STATUS dl_status)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            message(FATAL_ERROR "Failed to download ${name}: ${dl_status}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${ALICE2_DEPENDS_DIR}/${name}"
            WORKING_DIRECTORY "${ALICE2_DEPENDS_DIR}"
            RESULT_VARIABLE unpack_ret
        )
        if(NOT unpack_ret EQUAL 0)
            message(FATAL_ERROR "Failed to unpack ${name}")
        endif()

        file(REMOVE "${ALICE2_DEPENDS_DIR}/${name}")
        message(STATUS "Extracted and removed ${name}")
    endforeach()
endif()

set(GLEW_DIR "${ALICE2_DEPENDS_DIR}/glew")
set(GLEW_INCLUDE_DIRS "${GLEW_DIR}/include")
set(GLEW_LIBRARIES "${GLEW_DIR}/lib/glew32.lib")
if(NOT EXISTS ${GLEW_INCLUDE_DIRS}/GL/glew.h)
    message(FATAL_ERROR "GLEW headers not found at ${GLEW_INCLUDE_DIRS}")
endif()
if(NOT EXISTS ${GLEW_LIBRARIES})
    message(FATAL_ERROR "GLEW library not found at ${GLEW_LIBRARIES}")
endif()

set(GLFW_DIR "${ALICE2_DEPENDS_DIR}/glfw")
set(GLFW_INCLUDE_DIRS "${GLFW_DIR}/include")
if(BUILD_SHARED_LIBS)
    set(GLFW_LIBRARIES "${GLFW_DIR}/lib/glfw3dll.lib")
else()
    set(GLFW_LIBRARIES "${GLFW_DIR}/lib/glfw3.lib")
endif()
if(NOT EXISTS ${GLFW_INCLUDE_DIRS}/GLFW/glfw3.h)
    message(FATAL_ERROR "GLFW headers not found at ${GLFW_INCLUDE_DIRS}")
endif()
if(NOT EXISTS ${GLFW_LIBRARIES})
    message(FATAL_ERROR "GLFW library not found at ${GLFW_LIBRARIES}")
endif()

set(EIGEN_DIR "${ALICE2_DEPENDS_DIR}/eigen")
if(NOT EXISTS ${EIGEN_DIR}/eigen)
    message(FATAL_ERROR "Eigen headers not found at ${EIGEN_DIR}")
endif()

set(NLOHMANN_JSON_DIR "${ALICE2_DEPENDS_DIR}/nlohmann")
set(NLOHMANN_JSON_INCLUDE_DIRS "${NLOHMANN_JSON_DIR}/include")
if(NOT EXISTS ${NLOHMANN_JSON_INCLUDE_DIRS}/nlohmann/json.hpp)
    message(FATAL_ERROR "nlohmann/json headers not found at ${NLOHMANN_JSON_INCLUDE_DIRS}")
endif()

set(GL2PS_URL "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/gl2ps-1.4.2.zip")
set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps-1.4.2")
if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
    set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps")
endif()
if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
    get_filename_component(GL2PS_ARCHIVE_NAME "${GL2PS_URL}" NAME)
    message(STATUS "Downloading ${GL2PS_ARCHIVE_NAME} from ${GL2PS_URL}...")
    file(DOWNLOAD "${GL2PS_URL}" "${ALICE2_DEPENDS_DIR}/${GL2PS_ARCHIVE_NAME}" SHOW_PROGRESS STATUS gl2ps_dl_status)
    list(GET gl2ps_dl_status 0 gl2ps_dl_code)
    if(NOT gl2ps_dl_code EQUAL 0)
        message(FATAL_ERROR "Failed to download ${GL2PS_ARCHIVE_NAME}: ${gl2ps_dl_status}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${ALICE2_DEPENDS_DIR}/${GL2PS_ARCHIVE_NAME}"
        WORKING_DIRECTORY "${ALICE2_DEPENDS_DIR}"
        RESULT_VARIABLE gl2ps_unpack_ret
    )
    if(NOT gl2ps_unpack_ret EQUAL 0)
        message(FATAL_ERROR "Failed to unpack ${GL2PS_ARCHIVE_NAME}")
    endif()

    file(REMOVE "${ALICE2_DEPENDS_DIR}/${GL2PS_ARCHIVE_NAME}")
    message(STATUS "Extracted and removed ${GL2PS_ARCHIVE_NAME}")

    set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps-1.4.2")
    if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
        set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps")
    endif()
endif()
if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
    message(FATAL_ERROR "gl2ps header not found at ${ALICE2_DEPENDS_DIR}/gl2ps-1.4.2 or ${ALICE2_DEPENDS_DIR}/gl2ps")
endif()
set(GL2PS_INCLUDE_DIRS "${GL2PS_DIR}")
set(GL2PS_SOURCE "${GL2PS_DIR}/gl2ps.c")
if(NOT EXISTS "${GL2PS_SOURCE}")
    message(FATAL_ERROR "gl2ps source not found at ${GL2PS_SOURCE}")
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${EIGEN_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    ${GL2PS_INCLUDE_DIRS}
)
if(ALICE2_WITH_ZSPACE_CORE)
    include_directories(BEFORE
        "${ZSPACE_CORE_DIR}"
        "${ZSPACE_CORE_DIR}/include"
        "${ZSPACE_CORE_DIR}/third_party"
        "${ZSPACE_CORE_DIR}/third_party/depends"
    )
elseif(ALICE2_USING_ZSPACE_SDK)
    include_directories(BEFORE
        "${ZSPACE_SDK_DIR}/include"
    )
endif()
if(ALICE2_USING_TEST_MODE)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tests)
else()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/userSrc)
endif()
if(ALICE2_ENABLE_CUDA)
    include_directories(${CUDAToolkit_INCLUDE_DIRS})
endif()

file(GLOB_RECURSE ALICE2_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
set(ALICE2_SOURCES ${ALICE2_CORE_SOURCES})
list(APPEND ALICE2_SOURCES ${GL2PS_SOURCE})

if(ALICE2_USING_TEST_MODE)
    file(GLOB_RECURSE ALICE2_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    list(APPEND ALICE2_SOURCES ${ALICE2_TEST_SOURCES})
else()
    file(GLOB_RECURSE ALICE2_USER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.cpp"
    )
    list(APPEND ALICE2_SOURCES ${ALICE2_USER_SOURCES})
endif()

if(ALICE2_ENABLE_CUDA)
    file(GLOB_RECURSE ALICE2_CORE_CUDA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu"
    )
    set(ALICE2_CUDA_SOURCES ${ALICE2_CORE_CUDA_SOURCES})
    if(ALICE2_USING_TEST_MODE)
        file(GLOB_RECURSE ALICE2_TEST_CUDA_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cu"
        )
        list(APPEND ALICE2_CUDA_SOURCES ${ALICE2_TEST_CUDA_SOURCES})
    else()
        file(GLOB_RECURSE ALICE2_USER_CUDA_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.cu"
        )
        list(APPEND ALICE2_CUDA_SOURCES ${ALICE2_USER_CUDA_SOURCES})
    endif()
    list(APPEND ALICE2_SOURCES ${ALICE2_CUDA_SOURCES})
endif()

file(GLOB_RECURSE ALICE2_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
if(ALICE2_USING_TEST_MODE)
    file(GLOB_RECURSE ALICE2_TEST_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )
    list(APPEND ALICE2_HEADERS ${ALICE2_TEST_HEADERS})
else()
    file(GLOB_RECURSE ALICE2_USER_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.h"
    )
    list(APPEND ALICE2_HEADERS ${ALICE2_USER_HEADERS})
endif()

macro(group_sources_by_folder prefix files)
    foreach(file ${files})
        file(RELATIVE_PATH rel_file "${CMAKE_CURRENT_SOURCE_DIR}" "${file}")
        get_filename_component(folder "${rel_file}" PATH)
        string(REPLACE "/" "\\" group "${folder}")
        source_group("${prefix}\\${group}" FILES "${file}")
    endforeach()
endmacro()
group_sources_by_folder("Source Files" "${ALICE2_SOURCES}")
group_sources_by_folder("Header Files" "${ALICE2_HEADERS}")

add_executable(alice2 ${ALICE2_SOURCES} ${ALICE2_HEADERS})

if(ALICE2_ENABLE_CUDA)
    target_compile_definitions(alice2 PRIVATE ALICE2_WITH_CUDA=1)
else()
    target_compile_definitions(alice2 PRIVATE ALICE2_WITH_CUDA=0)
endif()
target_compile_definitions(alice2 PRIVATE JSON_SKIP_LIBRARY_VERSION_CHECK=1)

target_link_libraries(alice2
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${GLFW_LIBRARIES}
)
if(ALICE2_WITH_ZSPACE_CORE)
    target_link_libraries(alice2
        zspace::core
        zspace::interface
        zspace::io
    )
    target_compile_definitions(alice2 PRIVATE ALICE2_WITH_ZSPACE_CORE=1)
elseif(ALICE2_USING_ZSPACE_SDK)
    target_link_libraries(alice2
        "${ZSPACE_SDK_DIR}/lib/zSpace_Core.lib"
        "${ZSPACE_SDK_DIR}/lib/zSpace_Interface.lib"
        "${ZSPACE_SDK_DIR}/lib/zSpace_IO.lib"
    )
    target_compile_definitions(alice2 PRIVATE ALICE2_WITH_ZSPACE_CORE=1 ZSPACE_DYNAMIC_LIBRARY)
endif()
if(ALICE2_ENABLE_CUDA)
    target_link_libraries(alice2 CUDA::cublas CUDA::cudart)
endif()

if(WIN32)
    target_link_libraries(alice2 user32 gdi32 shell32)
endif()

add_custom_command(TARGET alice2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:alice2>/data"
    COMMENT "Creating runtime data directory")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_custom_command(TARGET alice2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:alice2>/data"
        COMMENT "Copying data directory to output directory")
endif()

if(MSVC)
    target_compile_options(alice2 PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4100>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4244>
    )
    target_compile_definitions(alice2 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(alice2 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(alice2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    set(GLEW_DLL_PATH "${ALICE2_DEPENDS_DIR}/glew/bin/glew32.dll")
    if(EXISTS ${GLEW_DLL_PATH})
        add_custom_command(TARGET alice2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${GLEW_DLL_PATH} $<TARGET_FILE_DIR:alice2>
            COMMENT "Copying glew32.dll to output directory")
    else()
        message(WARNING "glew32.dll not found at ${GLEW_DLL_PATH}")
    endif()

    set(GLFW_DLL_PATH "${ALICE2_DEPENDS_DIR}/glfw/bin/glfw3.dll")
    if(BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
        if(EXISTS ${GLFW_DLL_PATH})
            add_custom_command(TARGET alice2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${GLFW_DLL_PATH} $<TARGET_FILE_DIR:alice2>
                COMMENT "Copying glfw3.dll to output directory")
        else()
            message(WARNING "glfw3.dll not found at ${GLFW_DLL_PATH}")
        endif()
    else()
        message(STATUS "Using static GLFW library - no DLL copy needed")
    endif()

    if(ALICE2_WITH_ZSPACE_CORE)
        foreach(zspace_target zSpace_Core zSpace_Interface zSpace_IO)
            if(TARGET ${zspace_target})
                add_custom_command(TARGET alice2 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${zspace_target}> $<TARGET_FILE_DIR:alice2>
                    COMMENT "Copying ${zspace_target} DLL to alice2 output directory")
            endif()
        endforeach()
    elseif(ALICE2_USING_ZSPACE_SDK)
        foreach(zspace_dll zSpace_Core zSpace_Interface zSpace_IO)
            add_custom_command(TARGET alice2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ZSPACE_SDK_DIR}/bin/${zspace_dll}.dll" $<TARGET_FILE_DIR:alice2>
                COMMENT "Copying precompiled ${zspace_dll} DLL to output directory")
        endforeach()
    endif()
endif()

set(ALICE2_INSTALL_RUNTIME_DESTINATION "bin")
if(ALICE2_USING_TEST_MODE)
    set(ALICE2_INSTALL_RUNTIME_DESTINATION "build_tests")
endif()

install(TARGETS alice2
    RUNTIME DESTINATION ${ALICE2_INSTALL_RUNTIME_DESTINATION}
    LIBRARY DESTINATION _deps/lib
    ARCHIVE DESTINATION _deps/lib
)
if(ALICE2_USING_TEST_MODE)
    install(DIRECTORY tests/
        DESTINATION build_tests
        FILES_MATCHING PATTERN "*.cpp" PATTERN "*.h"
    )
else()
    install(DIRECTORY userSrc/
        DESTINATION userSrc/examples
        FILES_MATCHING PATTERN "*.cpp" PATTERN "*.h"
    )
endif()
install(DIRECTORY include/
    DESTINATION include/alice2
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
install(DIRECTORY ${GLFW_INCLUDE_DIRS}/
    DESTINATION include/dependencies/GLFW
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY ${GLEW_INCLUDE_DIRS}/
    DESTINATION include/dependencies
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY ${GL2PS_INCLUDE_DIRS}/
    DESTINATION include/dependencies/gl2ps
    FILES_MATCHING PATTERN "*.h"
)

if(WIN32)
    if(EXISTS ${GLEW_DLL_PATH})
        install(FILES ${GLEW_DLL_PATH} DESTINATION bin)
    endif()
    if(BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
        if(EXISTS ${GLFW_DLL_PATH})
            install(FILES ${GLFW_DLL_PATH} DESTINATION bin)
        endif()
    endif()
endif()

install(FILES CMakeLists.txt DESTINATION .)

message(STATUS "alice2 Configuration Summary:")
message(STATUS "  Build mode: ${ALICE2_BUILD_MODE}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA enabled: ${ALICE2_ENABLE_CUDA}")
message(STATUS "  zspace_core enabled: ${ALICE2_WITH_ZSPACE_CORE}")
if(ALICE2_WITH_ZSPACE_CORE)
    message(STATUS "  zspace_core dir: ${ZSPACE_CORE_DIR}")
endif()
if(ALICE2_ENABLE_CUDA)
    message(STATUS "  CUDA standard: ${CMAKE_CUDA_STANDARD}")
    message(STATUS "  CUDA archs: ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "  GLEW: ${GLEW_LIBRARIES}")
message(STATUS "  GLFW: ${GLFW_LIBRARIES} (precompiled)")
message(STATUS "  Eigen: ${EIGEN_DIR} (header-only)")
message(STATUS "  nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIRS} (header-only)")
message(STATUS "  gl2ps: ${GL2PS_INCLUDE_DIRS} (SVG export)")
