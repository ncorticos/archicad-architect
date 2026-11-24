cmake_minimum_required (VERSION 3.19)

include (Tools/CMakeCommon.cmake)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (NOT DEFINED AC_API_DEVKIT_DIR)
	message (FATAL_ERROR "Pass -DAC_API_DEVKIT_DIR=/path/to/API.Development.Kit/Support")
endif ()

set (ACAPINC_FILE_LOCATION "${AC_API_DEVKIT_DIR}/Inc/ACAPinc.h")
if (EXISTS ${ACAPINC_FILE_LOCATION})
	file (READ ${ACAPINC_FILE_LOCATION} ACAPIncContent)
	string (REGEX MATCHALL "#define[ \t]+ServerMainVers_([0-9][0-9])" VersionList ${ACAPIncContent})
	set (AC_VERSION ${CMAKE_MATCH_1})
else ()
	message (FATAL_ERROR "Failed to detect Archicad version from ACAPinc.h: File does not exist: ${ACAPINC_FILE_LOCATION}.")
endif ()

set (AC_USE_LOCAL_DEVKIT ON)

set (DEVKIT_BUILDNUM "3000")

if (NOT DEFINED AC_VERSION)
    message (FATAL_ERROR "AC_VERSION is not set!")
endif ()

message (STATUS "Archicad Version: ${AC_VERSION}")
SetGlobalCompilerDefinitions (${AC_VERSION})

ReadConfigJson ()
set (AC_ADDON_LANGUAGE "${addOnDefaultLanguage}" CACHE STRING "Add-On language code.")

project (${addOnName}-${AC_VERSION})

set (AC_ADDON_FOR_DISTRIBUTION OFF CACHE BOOL "")

set (AddOnSourcesFolder Src)
set (AddOnResourcesFolder .)

set (PCH_HEADER "")

GenerateAddOnProject (CMakeTarget ${AC_VERSION} "${AC_API_DEVKIT_DIR}" ${AddOnSourcesFolder} ${AddOnResourcesFolder} ${AC_ADDON_LANGUAGE} ${addOnDefaultLanguage} "${PCH_HEADER}")