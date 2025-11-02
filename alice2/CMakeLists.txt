cmake_minimum_required(VERSION 3.16)

project(alice2 VERSION 1.0.0 LANGUAGES CXX)

option(ALICE2_ENABLE_CUDA "Enable CUDA build of alice2" OFF)
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

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${EIGEN_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
)
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

target_link_libraries(alice2
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${GLFW_LIBRARIES}
)
if(ALICE2_ENABLE_CUDA)
    target_link_libraries(alice2 CUDA::cublas CUDA::cudart)
endif()

if(WIN32)
    target_link_libraries(alice2 user32 gdi32 shell32)
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
if(ALICE2_ENABLE_CUDA)
    message(STATUS "  CUDA standard: ${CMAKE_CUDA_STANDARD}")
    message(STATUS "  CUDA archs: ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "  GLEW: ${GLEW_LIBRARIES}")
message(STATUS "  GLFW: ${GLFW_LIBRARIES} (precompiled)")
message(STATUS "  Eigen: ${EIGEN_DIR} (header-only)")
message(STATUS "  nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIRS} (header-only)")
