cmake_minimum_required(VERSION 3.16)

project(alice2 VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directories - keep executables in bin, move libraries to _deps
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/lib)

# Set a cleaner build directory structure
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_deps/CMakeTargets")

# Find required packages
find_package(OpenGL REQUIRED)

# Use GLEW from ALICE_PLATFORM
set(ALICE_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ALICE_PLATFORM")
set(GLEW_INCLUDE_DIRS "${ALICE_PLATFORM_DIR}/src/glew/include")
set(GLEW_LIBRARIES "${ALICE_PLATFORM_DIR}/src/glew/lib/glew32.lib")

# Check if GLEW files exist
if(NOT EXISTS ${GLEW_INCLUDE_DIRS}/GL/glew.h)
    message(FATAL_ERROR "GLEW headers not found at ${GLEW_INCLUDE_DIRS}")
endif()
if(NOT EXISTS ${GLEW_LIBRARIES})
    message(FATAL_ERROR "GLEW library not found at ${GLEW_LIBRARIES}")
endif()

# Use GLFW from local depends directory
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/depends/glfw")
set(GLFW_INCLUDE_DIRS "${GLFW_DIR}/include")

# Check if GLFW files exist
if(NOT EXISTS ${GLFW_INCLUDE_DIRS}/GLFW/glfw3.h)
    message(FATAL_ERROR "GLFW headers not found at ${GLFW_INCLUDE_DIRS}")
endif()

# Build GLFW as a subdirectory (shared library for DLL generation)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)  # Force shared library build for GLFW DLL
add_subdirectory(${GLFW_DIR} _deps/glfw_build)

# Set GLFW target properties to organize in solution
set_target_properties(glfw PROPERTIES FOLDER "_deps/GLFW")

message(STATUS "Using GLFW from local depends directory")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
)

# Collect all .cpp files under src/ and userSrc/
file(GLOB_RECURSE ALICE2_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.cpp"
)

# Collect all .h files under include/, src/, and userSrc/
file(GLOB_RECURSE ALICE2_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.h"
)

# Helper macro to set up source groups based on folder structure
macro(group_sources_by_folder prefix files)
    foreach(file ${files})
        # Get the path relative to the project source dir
        file(RELATIVE_PATH rel_file "${CMAKE_CURRENT_SOURCE_DIR}" "${file}")
        # Extract the folder part (without filename)
        get_filename_component(folder "${rel_file}" PATH)
        # Replace / with \ for Visual Studio filters
        string(REPLACE "/" "\\" group "${folder}")
        # Assign to filter
        source_group("${prefix}\\${group}" FILES "${file}")
    endforeach()
endmacro()

# Group sources and headers
group_sources_by_folder("Source Files" "${ALICE2_SOURCES}")
group_sources_by_folder("Header Files" "${ALICE2_HEADERS}")

# Create the executable
add_executable(alice2 ${ALICE2_SOURCES} ${ALICE2_HEADERS})

# Link libraries
target_link_libraries(alice2
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    glfw
)

# Compiler-specific options
if(MSVC)
    target_compile_options(alice2 PRIVATE /W4 /wd4100 /wd4244)
    target_compile_definitions(alice2 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(alice2 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    target_compile_definitions(alice2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    # Copy DLLs to output directory for Windows
    # Copy GLEW DLL (manually placed in depends/glfw/)
    set(GLEW_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/glfw/glew32.dll")
    if(EXISTS ${GLEW_DLL_PATH})
        add_custom_command(TARGET alice2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GLEW_DLL_PATH} $<TARGET_FILE_DIR:alice2>
            COMMENT "Copying glew32.dll to output directory")
    else()
        message(WARNING "glew32.dll not found at ${GLEW_DLL_PATH}")
    endif()

    # Copy GLFW DLL (generated from build)
    add_custom_command(TARGET alice2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:glfw> $<TARGET_FILE_DIR:alice2>
        COMMENT "Copying GLFW DLL to output directory")
endif()

# Install targets for distribution
install(TARGETS alice2
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION _deps/lib
    ARCHIVE DESTINATION _deps/lib
)

# Install GLFW DLL and import library
install(TARGETS glfw
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION _deps/lib
    ARCHIVE DESTINATION _deps/lib
)

# Ensure DLLs are copied during install
install(FILES
    "$<TARGET_FILE:glfw>"
    DESTINATION bin
)

# Install user source directory with examples
install(DIRECTORY userSrc/
    DESTINATION userSrc/examples
    FILES_MATCHING PATTERN "*.cpp" PATTERN "*.h"
)

# Install core headers needed for user sketches
install(DIRECTORY include/
    DESTINATION include/alice2
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install dependency headers
install(DIRECTORY ${GLFW_INCLUDE_DIRS}/
    DESTINATION include/dependencies/GLFW
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY ${GLEW_INCLUDE_DIRS}/
    DESTINATION include/dependencies
    FILES_MATCHING PATTERN "*.h"
)

# Install GLEW DLL
set(GLEW_DLL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/depends/glfw/glew32.dll")
if(EXISTS ${GLEW_DLL_SOURCE})
    install(FILES ${GLEW_DLL_SOURCE}
        DESTINATION bin
    )
else()
    message(WARNING "GLEW DLL not found at ${GLEW_DLL_SOURCE} for install")
endif()

# Install build scripts and project files (keep main project files in root)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/alice2.sln"
    "${CMAKE_CURRENT_BINARY_DIR}/alice2.vcxproj"
    "${CMAKE_CURRENT_BINARY_DIR}/alice2.vcxproj.filters"
    DESTINATION .
    OPTIONAL
)

# Install CMake files for user sketch building
install(FILES CMakeLists.txt
    DESTINATION .
)

# Install dependency build files to _deps (for reference but hidden from main view)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/"
    DESTINATION _deps
    OPTIONAL
    PATTERN "*.tmp" EXCLUDE
    PATTERN "*.log" EXCLUDE
)

# Print configuration summary
message(STATUS "alice2 Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "  GLEW: ${GLEW_LIBRARIES}")
message(STATUS "  GLFW: glfw (built from source)")

# Optional: Create a build script for convenience
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build.bat.in
        ${CMAKE_CURRENT_BINARY_DIR}/build.bat
        @ONLY
    )
else()
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build.sh.in
        ${CMAKE_CURRENT_BINARY_DIR}/build.sh
        @ONLY
    )
endif()

# Create a script to organize build directory for distribution
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/organize_build.bat.in
        ${CMAKE_CURRENT_BINARY_DIR}/organize_build.bat
        @ONLY
    )
endif()
