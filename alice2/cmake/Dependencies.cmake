set(ALICE2_DEPENDS_DIR "${PROJECT_SOURCE_DIR}/depends")
set(GL2PS_URL "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/gl2ps-1.4.2.zip")
set(LIBIGL_URL "https://github.com/libigl/libigl/archive/refs/tags/v2.5.0.zip")

function(alice2_download_dependency url)
    get_filename_component(name "${url}" NAME)
    message(STATUS "Downloading ${name} from ${url}...")
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
endfunction()

find_package(OpenGL REQUIRED)
if(ALICE2_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()

set(ALICE2_HAS_SYSTEM_DEPS OFF)
set(ALICE2_HAS_SYSTEM_LIBIGL OFF)
if(ALICE2_USE_SYSTEM_DEPS)
    find_package(glfw3 CONFIG QUIET)
    find_package(Eigen3 CONFIG QUIET)
    find_package(nlohmann_json CONFIG QUIET)
    find_package(igl CONFIG QUIET)
    find_path(STB_INCLUDE_DIR
        NAMES stb_image_write.h stb_truetype.h
        PATH_SUFFIXES stb
    )

    if(TARGET glfw AND TARGET Eigen3::Eigen AND TARGET nlohmann_json::nlohmann_json AND STB_INCLUDE_DIR)
        set(ALICE2_HAS_SYSTEM_DEPS ON)
    else()
        if(MSVC)
            message(STATUS "System dependencies incomplete; falling back to alice2/depends")
        else()
            message(STATUS "System dependencies incomplete")
        endif()
    endif()

    if(TARGET igl::core)
        set(ALICE2_HAS_SYSTEM_LIBIGL ON)
    endif()
endif()

if(NOT ALICE2_HAS_SYSTEM_DEPS AND ALICE2_USE_SYSTEM_DEPS AND NOT MSVC)
    message(FATAL_ERROR
        "System dependencies are required for non-MSVC builds. "
        "Install glfw, Eigen3, nlohmann_json, and STB for your toolchain "
        "(for Windows/MSYS2 clang64: pacman -S --needed "
        "mingw-w64-clang-x86_64-glfw mingw-w64-clang-x86_64-eigen3 "
        "mingw-w64-clang-x86_64-nlohmann-json "
        "mingw-w64-clang-x86_64-stb)."
    )
endif()

if(NOT ALICE2_HAS_SYSTEM_DEPS)
    file(GLOB DEP_FILES "${ALICE2_DEPENDS_DIR}/*")
    if(NOT DEP_FILES)
        file(MAKE_DIRECTORY "${ALICE2_DEPENDS_DIR}")

        set(ASSET_URLS
            "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/eigen.zip"
            "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/glfw.zip"
            "${GL2PS_URL}"
            "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/nlohmann.zip"
            "https://github.com/GitZHCODE/zspace_alice2/releases/download/depends/stb.zip"
        )

        foreach(url IN LISTS ASSET_URLS)
            alice2_download_dependency("${url}")
        endforeach()
    endif()
else()
    file(MAKE_DIRECTORY "${ALICE2_DEPENDS_DIR}")
endif()

if(ALICE2_HAS_SYSTEM_LIBIGL)
    set(LIBIGL_INCLUDE_DIRS "")
    set(ALICE2_LIBIGL_TARGETS igl::core)
else()
    set(LIBIGL_DIR "${ALICE2_DEPENDS_DIR}/libigl")
if(NOT EXISTS "${LIBIGL_DIR}/include/igl/igl_inline.h")
        alice2_download_dependency("${LIBIGL_URL}")
        if(EXISTS "${ALICE2_DEPENDS_DIR}/libigl-2.5.0")
            file(RENAME "${ALICE2_DEPENDS_DIR}/libigl-2.5.0" "${LIBIGL_DIR}")
        endif()
    endif()
    if(NOT EXISTS "${LIBIGL_DIR}/include/igl/igl_inline.h")
        message(FATAL_ERROR "libigl headers not found at ${LIBIGL_DIR}/include")
    endif()
    set(LIBIGL_INCLUDE_DIRS "${LIBIGL_DIR}/include")
    set(ALICE2_LIBIGL_TARGETS "")
endif()

set(ALICE2_DIRECTIONAL_INCLUDE_DIRS "")
if(ALICE2_ENABLE_DIRECTIONAL)
    set(DIRECTIONAL_DIR "${ALICE2_DEPENDS_DIR}/directional")
    if(NOT EXISTS "${DIRECTIONAL_DIR}/include/directional/PCFaceTangentBundle.h")
        include(FetchContent)
        FetchContent_Declare(
            directional
            GIT_REPOSITORY https://github.com/avaxman/Directional.git
            GIT_TAG 25738730958f0bfa68289ae628a5cd3c4b719d61
            GIT_SHALLOW TRUE
        )
        FetchContent_Populate(directional)
        set(DIRECTIONAL_DIR "${directional_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${DIRECTIONAL_DIR}/include/directional/PCFaceTangentBundle.h")
        message(FATAL_ERROR "Directional headers not found at ${DIRECTIONAL_DIR}/include")
    endif()
    set(ALICE2_DIRECTIONAL_INCLUDE_DIRS "${DIRECTIONAL_DIR}/include")
endif()

# MIQ is provided by libigl's GPL/copyleft CoMiSo module. Keep it explicit so
# core libigl users do not acquire the dependency accidentally.
set(ALICE2_MIQ_TARGET "")
if(ALICE2_ENABLE_MIQ_REMESH)
    if(ALICE2_HAS_SYSTEM_LIBIGL)
        if(NOT TARGET igl_copyleft::comiso)
            message(FATAL_ERROR
                "ALICE2_ENABLE_MIQ_REMESH requires a system libigl with igl_copyleft::comiso. "
                "Configure with -DALICE2_ENABLE_MIQ_REMESH=OFF to omit this experiment.")
        endif()
    else()
        set(LIBIGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBIGL_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
        set(LIBIGL_COPYLEFT_CORE ON CACHE BOOL "" FORCE)
        set(LIBIGL_COPYLEFT_COMISO ON CACHE BOOL "" FORCE)
        add_subdirectory("${LIBIGL_DIR}" "${CMAKE_BINARY_DIR}/libigl-miq" EXCLUDE_FROM_ALL)
    endif()
    if(NOT TARGET igl_copyleft::comiso)
        message(FATAL_ERROR "libigl MIQ support was requested but igl_copyleft::comiso was not created")
    endif()
    # CoMiSo's bundled GMM 4.2 still uses the C++17-removed `register`
    # keyword. Clang promotes that diagnostic to an error in C++20 mode.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET CoMISo)
        target_compile_options(CoMISo PRIVATE -Wno-register)
    endif()
    if(MSVC AND TARGET CoMISo)
        # GMM 4.2 uses std::divides/multiplies without including <functional>.
        target_compile_options(CoMISo PRIVATE /FIfunctional)
        # This obsolete storage qualifier is rejected by MSVC in C++20 mode.
        # Patch the fetched header so the fix also covers libigl's MIQ adapter.
        get_target_property(ALICE2_COMISO_SOURCE_DIR CoMISo SOURCE_DIR)
        set(ALICE2_GMM_DOMAIN_HEADER "${ALICE2_COMISO_SOURCE_DIR}/ext/gmm-4.2/include/gmm/gmm_domain_decomp.h")
        if(EXISTS "${ALICE2_GMM_DOMAIN_HEADER}")
            file(READ "${ALICE2_GMM_DOMAIN_HEADER}" ALICE2_GMM_DOMAIN_SOURCE)
            string(REPLACE "register double a" "double a" ALICE2_GMM_DOMAIN_PATCHED "${ALICE2_GMM_DOMAIN_SOURCE}")
            if(NOT ALICE2_GMM_DOMAIN_SOURCE STREQUAL ALICE2_GMM_DOMAIN_PATCHED)
                file(WRITE "${ALICE2_GMM_DOMAIN_HEADER}" "${ALICE2_GMM_DOMAIN_PATCHED}")
            endif()
        endif()
    endif()
    set(ALICE2_MIQ_TARGET igl_copyleft::comiso)
endif()

if(WIN32)
    set(GLEW_DIR "${ALICE2_DEPENDS_DIR}/glew")
    set(GLEW_INCLUDE_DIRS "${GLEW_DIR}/include")
    set(GLEW_LIBRARIES "${GLEW_DIR}/lib/glew32s.lib")
    if(NOT EXISTS "${GLEW_INCLUDE_DIRS}/GL/glew.h")
        message(FATAL_ERROR "GLEW headers not found at ${GLEW_INCLUDE_DIRS}")
    endif()
    if(NOT EXISTS "${GLEW_LIBRARIES}")
        message(FATAL_ERROR "GLEW static library not found at ${GLEW_LIBRARIES}")
    endif()
else()
    set(GLEW_INCLUDE_DIRS "")
    set(GLEW_LIBRARIES "")
endif()

if(ALICE2_HAS_SYSTEM_DEPS)
    set(ALICE2_DEPENDENCY_MODE "system")
    set(GLFW_INCLUDE_DIRS "")
    set(EIGEN_DIR "")
    set(NLOHMANN_JSON_INCLUDE_DIRS "")
    set(STB_INCLUDE_DIRS "${STB_INCLUDE_DIR}")
    set(ALICE2_DEPENDENCY_TARGETS ${OPENGL_LIBRARIES} glfw Eigen3::Eigen nlohmann_json::nlohmann_json)
else()
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

    set(STB_INCLUDE_DIRS "${ALICE2_DEPENDS_DIR}/stb")
    if(NOT EXISTS ${STB_INCLUDE_DIRS}/stb_image_write.h OR NOT EXISTS ${STB_INCLUDE_DIRS}/stb_truetype.h)
        message(FATAL_ERROR "STB headers not found at ${STB_INCLUDE_DIRS}")
    endif()

    set(ALICE2_DEPENDENCY_TARGETS ${OPENGL_LIBRARIES} ${GLFW_LIBRARIES})
    set(ALICE2_DEPENDENCY_MODE "alice2/depends")
endif()

if(ALICE2_ENABLE_MIQ_REMESH AND TARGET Eigen3::Eigen)
    # Use the same Eigen as libigl/CoMiSo. The legacy bundled Eigen 3.3
    # shadows its fetched 3.4 headers and lacks MIQ's indexed-vector API.
    set(EIGEN_DIR "")
    list(APPEND ALICE2_DEPENDENCY_TARGETS Eigen3::Eigen)
endif()

set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps-1.4.2")
if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
    set(GL2PS_DIR "${ALICE2_DEPENDS_DIR}/gl2ps")
endif()
if(NOT EXISTS "${GL2PS_DIR}/gl2ps.h")
    alice2_download_dependency("${GL2PS_URL}")

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

