# Permanent viewer dependency. Core-only builds never include this file.
set(ALICE2_USD_ROOT "${PROJECT_SOURCE_DIR}/depends/USD_Lite" CACHE PATH "Reduced OpenUSD SDK")
set(ALICE2_USD_PLUGIN_DIR "" CACHE PATH "Override OpenUSD lib/usd resource directory")

# The legacy release is a Windows monolithic SDK without pxrConfig.cmake.
if(WIN32 AND NOT EXISTS "${ALICE2_USD_ROOT}/include/pxr/pxr.h" AND
   ALICE2_USD_ROOT STREQUAL "${PROJECT_SOURCE_DIR}/depends/USD_Lite")
    set(usd_archive "${PROJECT_SOURCE_DIR}/depends/USD_Lite.zip")
    file(DOWNLOAD
        "https://github.com/TaizhongC/server_usd/releases/download/depends/USD_Lite.zip"
        "${usd_archive}" STATUS status TLS_VERIFY ON
        EXPECTED_HASH SHA256=25404856fe00134aa8e1e3570fd913e256b2a5a5ffa9dc2d081d59bda208a307)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "USD Lite download failed: ${status}. Set ALICE2_USD_ROOT to a prepared SDK.")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${usd_archive}" DESTINATION "${PROJECT_SOURCE_DIR}/depends")
    file(REMOVE "${usd_archive}")
endif()

add_library(alice2_usd_sdk INTERFACE)
if(WIN32 AND EXISTS "${ALICE2_USD_ROOT}/lib/usd_ms.lib" AND
   NOT EXISTS "${ALICE2_USD_ROOT}/pxrConfig.cmake" AND
   NOT EXISTS "${ALICE2_USD_ROOT}/lib/cmake/pxr/pxrConfig.cmake")
    add_library(alice2_usd_monolithic SHARED IMPORTED GLOBAL)
    set_target_properties(alice2_usd_monolithic PROPERTIES
        IMPORTED_IMPLIB "${ALICE2_USD_ROOT}/lib/usd_ms.lib"
        IMPORTED_LOCATION "${ALICE2_USD_ROOT}/bin/usd_ms.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${ALICE2_USD_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS "PXR_MONOLITHIC_BUILD;NOMINMAX;ALICE2_USD_LEGACY_TBB")
    target_link_libraries(alice2_usd_sdk INTERFACE alice2_usd_monolithic)
    # Legacy TBB headers request tbb.lib through MSVC's auto-link pragmas.
    target_link_directories(alice2_usd_sdk INTERFACE "${ALICE2_USD_ROOT}/lib")
    set_property(TARGET alice2_usd_sdk PROPERTY ALICE2_USD_BIN "${ALICE2_USD_ROOT}/bin")
else()
    if(EXISTS "${ALICE2_USD_ROOT}/pxrConfig.cmake")
        set(pxr_DIR "${ALICE2_USD_ROOT}")
    elseif(EXISTS "${ALICE2_USD_ROOT}/lib/cmake/pxr/pxrConfig.cmake")
        set(pxr_DIR "${ALICE2_USD_ROOT}/lib/cmake/pxr")
    endif()
    find_package(pxr CONFIG QUIET HINTS "${ALICE2_USD_ROOT}" "${ALICE2_USD_ROOT}/lib/cmake/pxr")
    if(NOT pxr_FOUND)
        message(FATAL_ERROR
            "OpenUSD SDK missing. Run: python scripts/dependencies.py usd\n"
            "Or configure ALICE2_USD_ROOT / pxr_DIR to an existing SDK. Core-only builds remain available.")
    endif()
    if(TARGET usd_m)
        target_link_libraries(alice2_usd_sdk INTERFACE usd_m)
    elseif(TARGET usd_ms)
        target_link_libraries(alice2_usd_sdk INTERFACE usd_ms)
    elseif(TARGET usdGeom AND TARGET usdShade)
        target_link_libraries(alice2_usd_sdk INTERFACE usdGeom usdShade)
    else()
        message(FATAL_ERROR "OpenUSD package must provide usd_m, usd_ms or usdGeom + usdShade")
    endif()
    target_include_directories(alice2_usd_sdk SYSTEM INTERFACE ${PXR_INCLUDE_DIRS})
endif()
if(NOT ALICE2_USD_PLUGIN_DIR)
    if(EXISTS "${ALICE2_USD_ROOT}/lib/usd/plugInfo.json")
        set(ALICE2_USD_PLUGIN_DIR "${ALICE2_USD_ROOT}/lib/usd")
    elseif(PXR_CMAKE_DIR)
        # Packaged SDKs also locate their own resources relative to their libraries.
        get_filename_component(usd_prefix "${PXR_CMAKE_DIR}/../.." ABSOLUTE)
        if(EXISTS "${usd_prefix}/usd/plugInfo.json")
            set(ALICE2_USD_PLUGIN_DIR "${usd_prefix}/usd")
        endif()
    endif()
endif()
set_property(TARGET alice2_usd_sdk PROPERTY ALICE2_USD_RESOURCES "${ALICE2_USD_PLUGIN_DIR}")
