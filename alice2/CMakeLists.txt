cmake_minimum_required(VERSION 3.21)
project(alice2 VERSION 1.0.0 LANGUAGES C CXX)

option(ALICE2_BUILD_VIEWER "Build the viewer and research apps" ON)
option(ALICE2_ENABLE_CUDA "Enable CUDA build of alice2" OFF)
option(ALICE2_ENABLE_MIQ_REMESH "Enable libigl/CoMiSo MIQ remeshing" OFF)
option(ALICE2_ENABLE_DIRECTIONAL "Enable Directional integration" OFF)
option(ALICE2_USE_SYSTEM_DEPS "Prefer system-installed dependencies" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "Dependencies")
include(CTest)
include(cmake/Alice2Sketch.cmake)

function(alice2_library target)
    add_library(${target} STATIC EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${target} PUBLIC
        "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4100 /wd4244)
        target_compile_options(${target} PUBLIC /EHsc)
        target_compile_definitions(${target} PUBLIC _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# This target has no windowing, OpenGL, Eigen, or viewer dependency.
alice2_library(alice2_core
    src/utils/Vector.cpp src/utils/Matrix.cpp src/utils/Quaternion.cpp src/utils/Runtime.cpp
    src/core/Transform.cpp
    src/geometry/MeshData.cpp src/geometry/GraphData.cpp
    src/computeGeom/TensorField.cpp)
add_library(alice2::core ALIAS alice2_core)

if(BUILD_TESTING)
    add_executable(alice2_core_checks EXCLUDE_FROM_ALL checks/core.cpp)
    target_link_libraries(alice2_core_checks PRIVATE alice2_core)
    alice2_runtime(alice2_core_checks checks)
    add_test(NAME core COMMAND alice2_core_checks)
endif()
if(NOT ALICE2_BUILD_VIEWER)
    return()
endif()

if(ALICE2_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75)
    endif()
endif()
include(cmake/Dependencies.cmake)

add_library(alice2_eigen INTERFACE)
if(TARGET Eigen3::Eigen)
    target_link_libraries(alice2_eigen INTERFACE Eigen3::Eigen)
else()
    target_include_directories(alice2_eigen SYSTEM INTERFACE "${EIGEN_DIR}")
endif()
add_library(alice2_json INTERFACE)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(alice2_json INTERFACE nlohmann_json::nlohmann_json)
else()
    target_include_directories(alice2_json SYSTEM INTERFACE "${NLOHMANN_JSON_INCLUDE_DIRS}")
endif()
add_library(alice2_igl INTERFACE)
target_link_libraries(alice2_igl INTERFACE alice2_eigen ${ALICE2_LIBIGL_TARGETS})
target_include_directories(alice2_igl SYSTEM INTERFACE ${LIBIGL_INCLUDE_DIRS})

alice2_library(alice2_viewer
    src/core/Application.cpp src/core/Camera.cpp src/core/FontRenderer.cpp
    src/core/Renderer.cpp src/core/Scene.cpp
    src/input/InputManager.cpp src/input/CameraController.cpp
    src/ui/SimpleUI.cpp src/sketches/SketchManager.cpp
    src/objects/SceneObject.cpp src/objects/PrimitiveObject.cpp
    src/objects/ZSpaceObject.cpp src/objects/MeshObject.cpp src/objects/GraphObject.cpp
    src/computeGeom/ComputeMesh.cpp src/computeGeom/ComputeGraph.cpp
    ${GL2PS_SOURCE})
add_library(alice2::viewer ALIAS alice2_viewer)
target_link_libraries(alice2_viewer PUBLIC alice2_core ${OPENGL_LIBRARIES} ${GLEW_LIBRARIES}
    PRIVATE alice2_json)
if(TARGET glfw)
    target_link_libraries(alice2_viewer PUBLIC glfw)
else()
    target_link_libraries(alice2_viewer PUBLIC "${GLFW_LIBRARIES}")
endif()
target_include_directories(alice2_viewer SYSTEM PUBLIC
    ${OPENGL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS} ${GLFW_INCLUDE_DIRS})
target_include_directories(alice2_viewer SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${GL2PS_INCLUDE_DIRS})
if(WIN32)
    target_compile_definitions(alice2_viewer PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX GLEW_STATIC)
    target_link_libraries(alice2_viewer PRIVATE user32 gdi32 shell32)
endif()

# Existing algorithms remain in their source folders; only requested modules build.
alice2_library(alice2_fields src/computeGeom/scalarField.cpp
    src/computeGeom/ScalarField3D.cpp src/computeGeom/FieldStack.cpp)
target_link_libraries(alice2_fields PUBLIC alice2_viewer alice2_json)
alice2_library(alice2_projection src/solvers/ProjectionSolver.cpp src/solvers/ProjectionConstraints.cpp)
target_link_libraries(alice2_projection PUBLIC alice2_viewer alice2_eigen)
alice2_library(alice2_analysis src/analysis/MeshAnalyzer.cpp src/analysis/StressAnalyzer.cpp)
target_link_libraries(alice2_analysis PUBLIC alice2_viewer alice2_eigen)
alice2_library(alice2_tna src/tna/TnaSolver.cpp src/tna/BestFitForceDensitySolver.cpp
    src/tna/BestFitTnaSolver.cpp src/tna/TnaOffsetSolver.cpp)
target_link_libraries(alice2_tna PUBLIC alice2_viewer alice2_eigen)
alice2_library(alice2_fdm src/fdm/ProjectedMixedFDM.cpp)
target_link_libraries(alice2_fdm PUBLIC alice2_core alice2_eigen)
alice2_library(alice2_stacking src/stack/RuledSurfaceStackGeometry.cpp
    src/stack/RuledSurfaceStackIntervals.cpp src/stack/RuledSurfaceStackSolver.cpp)
target_link_libraries(alice2_stacking PUBLIC alice2_core alice2_eigen)
alice2_library(alice2_stereotomy src/geometry/Stereotomy.cpp)
target_link_libraries(alice2_stereotomy PUBLIC alice2_viewer)
alice2_library(alice2_remeshing src/remeshing/MiqFieldRemesher.cpp)
target_link_libraries(alice2_remeshing PUBLIC alice2_viewer PRIVATE alice2_igl ${ALICE2_MIQ_TARGET})
target_compile_definitions(alice2_remeshing PUBLIC
    ALICE2_WITH_MIQ=$<BOOL:${ALICE2_ENABLE_MIQ_REMESH}>
    ALICE2_WITH_DIRECTIONAL=$<BOOL:${ALICE2_ENABLE_DIRECTIONAL}>)
target_include_directories(alice2_remeshing SYSTEM PRIVATE ${ALICE2_DIRECTIONAL_INCLUDE_DIRS})
if(ALICE2_ENABLE_MIQ_REMESH AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(alice2_remeshing PRIVATE -Wno-register)
endif()
alice2_library(alice2_wave_latent src/ML/WaveLatent/WaveLatent.cpp src/ML/WaveLatent/WaveLatentNavigator.cpp)
target_link_libraries(alice2_wave_latent PUBLIC alice2_fields)

if(ALICE2_ENABLE_CUDA)
    file(GLOB_RECURSE ALICE2_CUDA_SOURCES CONFIGURE_DEPENDS src/*.cu)
    if(ALICE2_CUDA_SOURCES)
        target_sources(alice2_fields PRIVATE ${ALICE2_CUDA_SOURCES})
    endif()
    target_link_libraries(alice2_fields PUBLIC CUDA::cublas CUDA::cudart)
endif()
target_compile_definitions(alice2_fields PUBLIC ALICE2_WITH_CUDA=$<BOOL:${ALICE2_ENABLE_CUDA}>)

# Compile the common entry point once, including when several sketch apps are built.
add_library(alice2_entry OBJECT EXCLUDE_FROM_ALL src/main.cpp)
target_link_libraries(alice2_entry PRIVATE alice2_viewer)

# All configured research apps and interactive test apps, including future folders.
add_custom_target(user_all)
add_subdirectory(userSrc)
# Empty lists remain valid folder targets, including newly added folders.
get_property(folders DIRECTORY "${PROJECT_SOURCE_DIR}/userSrc" PROPERTY SUBDIRECTORIES)
foreach(folder IN LISTS folders)
    get_filename_component(group "${folder}" NAME)
    if(NOT TARGET user_${group})
        add_custom_target(user_${group})
    endif()
    set_property(GLOBAL APPEND PROPERTY ALICE2_SKETCH_GROUPS "${group}")
endforeach()
add_subdirectory(tests)

if(BUILD_TESTING)
    add_executable(alice2_checks EXCLUDE_FROM_ALL checks/main.cpp
        src/fdm/ProjectedMixedFDMChecks.cpp src/tna/BestFitTnaChecks.cpp src/tna/TnaOffsetChecks.cpp)
    target_link_libraries(alice2_checks PRIVATE alice2_tna alice2_fdm)
    alice2_runtime(alice2_checks checks)
    add_dependencies(user_all alice2_checks_runtime alice2_core_checks_runtime)

    add_test(NAME projected_mixed_fdm COMMAND alice2_checks --check-projected-mixed-fdm)
    add_test(NAME bestfit_tna COMMAND alice2_checks --check-bestfit-tna)
endif()

# Run scripts read the configured app list instead of stale executable files.
get_property(sketch_groups GLOBAL PROPERTY ALICE2_SKETCH_GROUPS)
list(REMOVE_DUPLICATES sketch_groups)
foreach(group IN LISTS sketch_groups)
    get_property(apps GLOBAL PROPERTY ALICE2_SKETCHES_${group})
    list(JOIN apps "\n" app_lines)
    file(WRITE "${CMAKE_BINARY_DIR}/groups/${group}.txt" "${app_lines}\n")
endforeach()
