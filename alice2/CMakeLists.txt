cmake_minimum_required(VERSION 3.21)
project(alice2 VERSION 1.0.0 LANGUAGES C CXX)

option(ALICE2_BUILD_VIEWER "Build the viewer and scene I/O libraries" ON)
option(ALICE2_ENABLE_CUDA "Enable CUDA build of alice2" OFF)
option(ALICE2_ENABLE_MIQ_REMESH "Enable libigl/CoMiSo MIQ remeshing" OFF)
option(ALICE2_ENABLE_DIRECTIONAL "Enable Directional integration" OFF)
option(ALICE2_USE_SYSTEM_DEPS "Prefer system-installed dependencies" ON)
option(ALICE2_BUILD_RESEARCH "Build bundled research apps and algorithm targets" ${PROJECT_IS_TOP_LEVEL})
option(ALICE2_BUILD_GPU_CHECKS "Build mesh rendering checks/benchmark (requires an OpenGL display)" OFF)

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
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/include" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src")
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
include(cmake/USD.cmake)

alice2_library(alice2_scene_io src/io/SceneIO.cpp)
add_library(alice2::scene_io ALIAS alice2_scene_io)
target_link_libraries(alice2_scene_io PUBLIC alice2_core PRIVATE alice2_usd_sdk)
if(ALICE2_USD_PLUGIN_DIR)
    target_compile_definitions(alice2_scene_io PRIVATE ALICE2_USD_PLUGIN_DIR="${ALICE2_USD_PLUGIN_DIR}")
endif()
if(BUILD_TESTING)
    add_executable(alice2_scene_io_checks EXCLUDE_FROM_ALL checks/scene_io.cpp)
    target_link_libraries(alice2_scene_io_checks PRIVATE alice2_scene_io)
    alice2_runtime(alice2_scene_io_checks checks)
    add_test(NAME scene_io COMMAND alice2_scene_io_checks)
endif()

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
if(ALICE2_BUILD_RESEARCH)
    add_library(alice2_igl INTERFACE)
    target_link_libraries(alice2_igl INTERFACE alice2_eigen ${ALICE2_LIBIGL_TARGETS})
    target_include_directories(alice2_igl SYSTEM INTERFACE ${LIBIGL_INCLUDE_DIRS})
endif()

alice2_library(alice2_viewer
    src/core/Application.cpp src/core/Camera.cpp src/core/FontRenderer.cpp
    src/core/Renderer.cpp src/core/GpuMeshRenderer.cpp src/core/Scene.cpp
    src/input/InputManager.cpp src/input/CameraController.cpp
    src/ui/SimpleUI.cpp src/sketches/SketchManager.cpp
    src/objects/SceneObject.cpp src/objects/PrimitiveObject.cpp
    src/objects/ZSpaceObject.cpp src/objects/MeshObject.cpp src/objects/GraphObject.cpp
    src/io/SceneObjects.cpp
    src/computeGeom/ComputeMesh.cpp src/computeGeom/ComputeGraph.cpp
    ${GL2PS_SOURCE})
add_library(alice2::viewer ALIAS alice2_viewer)
target_link_libraries(alice2_viewer PUBLIC alice2_core alice2_scene_io ${OPENGL_LIBRARIES} ${GLEW_LIBRARIES}
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
    if(BUILD_SHARED_LIBS AND EXISTS "${GLFW_DIR}/bin/glfw3.dll")
        set_property(TARGET alice2_viewer PROPERTY ALICE2_EXTRA_RUNTIME_DLLS "${GLFW_DIR}/bin/glfw3.dll")
    endif()
endif()
if(BUILD_TESTING)
    add_executable(alice2_scene_objects_checks EXCLUDE_FROM_ALL checks/scene_objects.cpp)
    target_link_libraries(alice2_scene_objects_checks PRIVATE alice2_viewer)
    alice2_runtime(alice2_scene_objects_checks checks)
    add_test(NAME scene_objects COMMAND alice2_scene_objects_checks)
endif()

# Available to external applications even when bundled research apps are skipped.
if(ALICE2_BUILD_GPU_CHECKS)
    add_executable(alice2_gpu_mesh_checks EXCLUDE_FROM_ALL checks/gpu_mesh.cpp)
    target_link_libraries(alice2_gpu_mesh_checks PRIVATE alice2_viewer)
    alice2_runtime(alice2_gpu_mesh_checks checks)
    if(BUILD_TESTING)
        add_test(NAME gpu_mesh COMMAND alice2_gpu_mesh_checks)
    endif()
endif()

# Available to external applications even when bundled research apps are skipped.
add_library(alice2_entry OBJECT EXCLUDE_FROM_ALL src/main.cpp)
target_link_libraries(alice2_entry PRIVATE alice2_viewer)
if(NOT ALICE2_BUILD_RESEARCH)
    return()
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
alice2_library(alice2_bricks src/bricks/BrickGenerator.cpp src/bricks/BrickPopulation.cpp
    src/bricks/BrickPlanes.cpp)
target_link_libraries(alice2_bricks PUBLIC alice2_eigen PRIVATE alice2_igl)
if(WIN32)
    target_compile_definitions(alice2_bricks PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_library(alice2::bricks ALIAS alice2_bricks)
if(BUILD_TESTING)
    add_executable(alice2_bricks_checks EXCLUDE_FROM_ALL checks/bricks.cpp checks/brick_population.cpp checks/brick_planes.cpp)
    target_link_libraries(alice2_bricks_checks PRIVATE alice2_bricks)
    alice2_runtime(alice2_bricks_checks checks)
    add_test(NAME bricks_fields COMMAND alice2_bricks_checks)
endif()
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
    add_dependencies(user_all alice2_checks_runtime alice2_core_checks_runtime
        alice2_scene_io_checks_runtime alice2_scene_objects_checks_runtime)

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
