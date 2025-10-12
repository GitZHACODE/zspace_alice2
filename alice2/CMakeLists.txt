cmake_minimum_required(VERSION 3.16)

project(alice2 VERSION 1.0.0 LANGUAGES CXX CUDA)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

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
find_package(CUDAToolkit REQUIRED)

# Use depends path
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
    file(DOWNLOAD
      "${url}"
      "${ALICE2_DEPENDS_DIR}/${name}"
      SHOW_PROGRESS
      STATUS dl_status
    )
    list(GET dl_status 0 dl_code)
    if(NOT dl_code EQUAL 0)
      message(FATAL_ERROR "Failed to download ${name}: ${dl_status}")
    endif()

    # unpack each ZIP directly into depends folder
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xzf "${ALICE2_DEPENDS_DIR}/${name}"
      WORKING_DIRECTORY "${ALICE2_DEPENDS_DIR}"
      RESULT_VARIABLE unpack_ret
    )
    if(NOT unpack_ret EQUAL 0)
      message(FATAL_ERROR "Failed to unpack ${name}")
    endif()

    # Clean up the zip file after successful extraction
    file(REMOVE "${ALICE2_DEPENDS_DIR}/${name}")
    message(STATUS "Extracted and removed ${name}")
  endforeach()
endif()

# Use GLEW from ALICE_PLATFORM
set(GLEW_DIR "${ALICE2_DEPENDS_DIR}/glew")
set(GLEW_INCLUDE_DIRS "${GLEW_DIR}/include")
set(GLEW_LIBRARIES "${GLEW_DIR}/lib/glew32.lib")

# Check if GLEW files exist
if(NOT EXISTS ${GLEW_INCLUDE_DIRS}/GL/glew.h)
    message(FATAL_ERROR "GLEW headers not found at ${GLEW_INCLUDE_DIRS}")
endif()
if(NOT EXISTS ${GLEW_LIBRARIES})
    message(FATAL_ERROR "GLEW library not found at ${GLEW_LIBRARIES}")
endif()

# Use precompiled GLFW from local depends directory
set(GLFW_DIR "${ALICE2_DEPENDS_DIR}/glfw")
set(GLFW_INCLUDE_DIRS "${GLFW_DIR}/include")

# Choose appropriate GLFW library based on build configuration
# glfw3.lib - static library for static linking
# glfw3dll.lib - import library for dynamic linking with glfw3.dll
# glfw3_mt.lib - static library with multithreading support
if(BUILD_SHARED_LIBS)
    set(GLFW_LIBRARIES "${GLFW_DIR}/lib/glfw3dll.lib")
else()
    set(GLFW_LIBRARIES "${GLFW_DIR}/lib/glfw3.lib")
endif()

# Check if GLFW files exist
if(NOT EXISTS ${GLFW_INCLUDE_DIRS}/GLFW/glfw3.h)
    message(FATAL_ERROR "GLFW headers not found at ${GLFW_INCLUDE_DIRS}")
endif()
if(NOT EXISTS ${GLFW_LIBRARIES})
    message(FATAL_ERROR "GLFW library not found at ${GLFW_LIBRARIES}")
endif()

message(STATUS "Using precompiled GLFW from local depends directory")
message(STATUS "GLFW library: ${GLFW_LIBRARIES}")

# Eigen library setup (header-only)
set(EIGEN_DIR "${ALICE2_DEPENDS_DIR}/eigen")
if(NOT EXISTS ${EIGEN_DIR}/eigen)
    message(FATAL_ERROR "Eigen headers not found at ${EIGEN_DIR}")
endif()

# nlohmann/json library setup (header-only)
set(NLOHMANN_JSON_DIR "${ALICE2_DEPENDS_DIR}/nlohmann")
set(NLOHMANN_JSON_INCLUDE_DIRS "${NLOHMANN_JSON_DIR}/include")
if(NOT EXISTS ${NLOHMANN_JSON_INCLUDE_DIRS}/nlohmann/json.hpp)
    message(FATAL_ERROR "nlohmann/json headers not found at ${NLOHMANN_JSON_INCLUDE_DIRS}")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${EIGEN_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    ${CUDAToolkit_INCLUDE_DIRS}
)

# Collect all .cpp files under src/ and userSrc/
file(GLOB_RECURSE ALICE2_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/userSrc/*.cpp"
)

# Collect all .cu files under src/
file(GLOB_RECURSE ALICE2_CUDA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu"
)
list(APPEND ALICE2_SOURCES ${ALICE2_CUDA_SOURCES})

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
    ${GLFW_LIBRARIES}
    CUDA::cublas
    CUDA::cudart
)

# Platform-specific linking for GLFW
if(WIN32)
    # Link Windows system libraries required by GLFW
    target_link_libraries(alice2
        user32
        gdi32
        shell32
    )
endif()

# Compiler-specific options
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

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    target_compile_definitions(alice2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    # Copy DLLs to output directory for Windows
    # Copy GLEW DLL
    set(GLEW_DLL_PATH "${ALICE2_DEPENDS_DIR}/glew/bin/glew32.dll")
    if(EXISTS ${GLEW_DLL_PATH})
        add_custom_command(TARGET alice2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GLEW_DLL_PATH} $<TARGET_FILE_DIR:alice2>
            COMMENT "Copying glew32.dll to output directory")
    else()
        message(WARNING "glew32.dll not found at ${GLEW_DLL_PATH}")
    endif()

    # Copy GLFW DLL (only needed when using dynamic linking)
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

# Install targets for distribution
install(TARGETS alice2
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION _deps/lib
    ARCHIVE DESTINATION _deps/lib
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

# Install DLLs
if(WIN32)
    # Install GLEW DLL
    if(EXISTS ${GLEW_DLL_PATH})
        install(FILES ${GLEW_DLL_PATH}
            DESTINATION bin
        )
    else()
        message(WARNING "GLEW DLL not found at ${GLEW_DLL_PATH} for install")
    endif()

    # Install GLFW DLL (only needed when using dynamic linking)
    if(BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
        if(EXISTS ${GLFW_DLL_PATH})
            install(FILES ${GLFW_DLL_PATH}
                DESTINATION bin
            )
        else()
            message(WARNING "GLFW DLL not found at ${GLFW_DLL_PATH} for install")
        endif()
    endif()
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

# Print configuration summary
message(STATUS "alice2 Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "  GLEW: ${GLEW_LIBRARIES}")
message(STATUS "  GLFW: ${GLFW_LIBRARIES} (precompiled)")
message(STATUS "  Eigen: ${EIGEN_DIR} (header-only)")
message(STATUS "  nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIRS} (header-only)")

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
