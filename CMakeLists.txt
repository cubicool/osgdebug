cmake_minimum_required(VERSION 3.19)

project(osgx VERSION 0.1.0)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(OSGX_IS_TOP_LEVEL ON)
else()
	set(OSGX_IS_TOP_LEVEL OFF)
endif()

option(OSGX_BUILD_EXAMPLES "Build the osgx examples" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_UTILS "Build the osgx utilities" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_PYTHON "Build the osgx Python module" ${OSGX_IS_TOP_LEVEL})
option(OSGX_INSTALL "Generate osgx installation rules" ${OSGX_IS_TOP_LEVEL})

if(WIN32)
	set(OSGX_WARNING_OPTIONS
		/W4
		/WX
		/wd4100
		/analyze
	)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	set(OSGX_WARNING_OPTIONS
		-W
		-Wall
		-Wshadow
		-Wconversion
		-Wsign-conversion
		-Wfloat-conversion
		-Wpedantic
		-Wextra
		-Werror
		-Wno-unused-parameter
	)
else()
	set(OSGX_WARNING_OPTIONS)
endif()

# add_subdirectory("OSG") # EXCLUDE_FROM_ALL)

find_package(OpenSceneGraph REQUIRED
	osg
	osgDB
	osgGA
	osgViewer
	osgUtil
	osgFX
	osgText
	osgWidget
	osgAnimation
)
find_package(OpenGL REQUIRED)

# Detected once here (not per-subdirectory/per-target) so OSGX_WITH_IMGUI and the
# IMGUI_* variables are already set by the time add_subdirectory(examples)/ext are
# reached below -- CMake subdirectories inherit the parent's variables at the point
# add_subdirectory() is called. REQUIRED because osgx::platform's Linux.cpp (X11/XRandr) is an
# unconditional part of libosgx -- see the X11 pkg_check_modules() call below.
find_package(PkgConfig REQUIRED)

pkg_check_modules(IMGUI imgui)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xrandr)
pkg_check_modules(EGL egl)
pkg_check_modules(GBM gbm libdrm)

option(OSGX_WITH_IMGUI "Compile osgx::imgui (Dear ImGui overlay) into libosgx" ${IMGUI_FOUND})

if(OSGX_WITH_IMGUI AND NOT IMGUI_FOUND)
	message(FATAL_ERROR "OSGX_WITH_IMGUI is ON but Dear ImGui was not found via pkg-config")
endif()

# osgx::platform's Linux.cpp (X11/XRandr) is not optional -- osgViewer already requires X11 for
# its GLX backend on Linux, so this carries no more risk than the existing OpenGL::GL dependency
# below. GraphicsWindowEGL/GBM are genuinely optional: EGL and GBM+libdrm are niche dev packages
# a box building osgx may not have installed, so they get the same auto-detected option treatment
# as OSGX_WITH_IMGUI.
option(OSGX_WITH_EGL "Compile osgx::platform's EGL-backed GraphicsWindowEGL into libosgx" ${EGL_FOUND})
option(OSGX_WITH_GBM "Compile osgx::platform's GBM/DRM-backed GraphicsWindowGBM into libosgx" ${GBM_FOUND})

if(OSGX_WITH_EGL AND NOT EGL_FOUND)
	message(FATAL_ERROR "OSGX_WITH_EGL is ON but EGL was not found via pkg-config")
endif()

if(OSGX_WITH_GBM AND NOT GBM_FOUND)
	message(FATAL_ERROR "OSGX_WITH_GBM is ON but GBM/libdrm was not found via pkg-config")
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/osgx/Version.hpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp"
	@ONLY
)

add_library(osgx_core INTERFACE)
add_library(osgx::core ALIAS osgx_core)

target_compile_features(osgx_core INTERFACE cxx_std_20)
target_include_directories(osgx_core INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
	"$<BUILD_INTERFACE:${OPENSCENEGRAPH_INCLUDE_DIRS}>"
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(osgx_core INTERFACE "$<BUILD_INTERFACE:${OPENSCENEGRAPH_LIBRARIES}>")
target_sources(osgx_core INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Warnings.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Core.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Array.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Callbacks.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Visitors.hpp>"
)
set_target_properties(osgx_core PROPERTIES EXPORT_NAME core)

add_library(osgx
	src/CaptureCubeMap.cpp
	src/Cursor.cpp
	src/Debug.cpp
	src/GGXPrefilter.cpp
	src/Grid.cpp
	src/IBL.cpp
	src/LambertianBake.cpp
	src/Linux.cpp
	src/Manipulators.cpp
	src/PBR.cpp
	src/Picking.cpp
	src/Shader.cpp
)
add_library(osgx::osgx ALIAS osgx)

target_compile_features(osgx PUBLIC cxx_std_20)
target_link_libraries(osgx
	PUBLIC osgx::core
	PRIVATE OpenGL::GL PkgConfig::X11
)
target_sources(osgx PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/osgx.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Grid.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/IBL.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/CaptureCubeMap.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/GGXPrefilter.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/LambertianBake.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Manipulators.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/PBR.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Picking.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Shader.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Debug.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/ImGui.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Linux.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/GraphicsWindowEGL.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/GraphicsWindowGBM.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Cursor.hpp>"
)
set_target_properties(osgx PROPERTIES
	EXPORT_NAME osgx
	POSITION_INDEPENDENT_CODE ON
)

target_compile_options(osgx PRIVATE ${OSGX_WARNING_OPTIONS})

if(OSGX_WITH_IMGUI)
	target_sources(osgx PRIVATE src/ImGui.cpp)
	target_compile_definitions(osgx PUBLIC OSGX_IMGUI)
	target_include_directories(osgx PUBLIC ${IMGUI_INCLUDE_DIRS})
	target_link_libraries(osgx PUBLIC ${IMGUI_LINK_LIBRARIES})
endif()

if(OSGX_WITH_EGL)
	target_sources(osgx PRIVATE src/GraphicsWindowEGL.cpp)
	target_compile_definitions(osgx PUBLIC OSGX_EGL)
	target_include_directories(osgx PRIVATE ${EGL_INCLUDE_DIRS})
	target_link_libraries(osgx PRIVATE ${EGL_LINK_LIBRARIES})
endif()

if(OSGX_WITH_GBM)
	target_sources(osgx PRIVATE src/GraphicsWindowGBM.cpp)
	target_compile_definitions(osgx PUBLIC OSGX_GBM)
	target_include_directories(osgx PRIVATE ${GBM_INCLUDE_DIRS})
	target_link_libraries(osgx PRIVATE ${GBM_LINK_LIBRARIES})
endif()

if(OSGX_INSTALL)
	install(TARGETS osgx_core osgx EXPORT osgxTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	)
	install(DIRECTORY osgx/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgx"
		FILES_MATCHING PATTERN "*.hpp"
	)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp"
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgx"
	)
	install(EXPORT osgxTargets
		FILE osgxTargets.cmake
		NAMESPACE osgx::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgx"
	)

	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/osgxConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/osgxConfig.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgx"
	)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/osgxConfig.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgx"
	)

	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
		@ONLY
	)
	add_custom_target(uninstall
		COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
		COMMENT "Removing files listed in install_manifest.txt"
	)
endif()

if(OSGX_BUILD_PYTHON)
	find_package(Python3 COMPONENTS Interpreter Development)
endif()
# add_subdirectory("/home/cubicool/dev/slughorn/ext/pybind11" "ext/pybind11")

if(OSGX_IS_TOP_LEVEL AND isMultiConfig)
	if(NOT "Asan" IN_LIST CMAKE_CONFIGURATION_TYPES)
		list(APPEND CMAKE_CONFIGURATION_TYPES Asan)
	endif()

elseif(OSGX_IS_TOP_LEVEL)
	set(allowedBuildTypes Asan Debug Release RelWithDebInfo MinSizeRel)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${allowedBuildTypes}")

	if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE IN_LIST allowedBuildTypes)
		message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
	endif()
endif()

set(CMAKE_C_FLAGS_ASAN
	"${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
	"Flags used by the C compiler for Asan build type or configuration." FORCE
)

set(CMAKE_CXX_FLAGS_ASAN
	"${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
	"Flags used by the C++ compiler for Asan build type or configuration." FORCE
)

set(CMAKE_EXE_LINKER_FLAGS_ASAN
	"${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
	"Linker flags to be used to create executables for Asan build type." FORCE
)

set(CMAKE_SHARED_LINKER_FLAGS_ASAN
	"${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
	"Linker lags to be used to create shared libraries for Asan build type." FORCE
)

function(OSGX_COMPILE_OPTIONS _target)
	target_compile_features("${_target}" PUBLIC cxx_std_20)
	target_link_libraries("${_target}" PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
	target_include_directories("${_target}" PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})
	target_compile_options("${_target}" PRIVATE ${OSGX_WARNING_OPTIONS})
endfunction()

function(OSGX_ADD_EXECUTABLE _prefix _topic)
	set(_target "${_prefix}-${_topic}")

	add_executable("${_target}"
		"${_target}.cpp"
		"../osgx/osgx.hpp"
	)

	target_link_libraries("${_target}" PRIVATE osgx::osgx)
	osgx_compile_options("${_target}")
	# Examples must exercise this checkout's public headers, not an older copy installed alongside
	# OSG. The OpenSceneGraph package contributes its prefix before transitive osgx includes, so make
	# the source root explicitly first.
	target_include_directories("${_target}" BEFORE PRIVATE "${PROJECT_SOURCE_DIR}")
endfunction()

if(OSGX_BUILD_EXAMPLES)
	add_subdirectory("examples")
endif()

if(OSGX_BUILD_UTILS)
	add_subdirectory("utils")
endif()
# add_subdirectory("plugins") # TODO: Disabled

if(OSGX_BUILD_PYTHON AND Python3_Development_FOUND)
	# TODO: Make this an option too!
	add_subdirectory("/home/cubicool/dev/slughorn/ext/pybind11" "ext/pybind11")

	pybind11_add_module(osgx_python "ext/osgx-python.cpp")

	set_target_properties(osgx_python PROPERTIES OUTPUT_NAME "osgx")
	target_include_directories(osgx_python PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		${OPENSCENEGRAPH_INCLUDE_DIRS}
		"/home/cubicool/dev/OpenSceneGraph.py"
		"/home/cubicool/dev/OpenSceneGraph.py/etc"
	)
	target_link_libraries(osgx_python PRIVATE
		osgx::osgx
		pybind11::module
	)
	osgx_compile_options(osgx_python)
	set_target_properties(osgx_python PROPERTIES POSITION_INDEPENDENT_CODE ON)

	# osgx.imgui Python bindings ride along automatically: osgx_python links
	# osgx::osgx, which already carries OSGX_IMGUI + the Dear ImGui include dirs
	# and link libraries as PUBLIC usage requirements when OSGX_WITH_IMGUI is on.
	if(NOT OSGX_WITH_IMGUI)
		message(STATUS "OSGX_WITH_IMGUI is OFF; osgx.imgui Python bindings will be unavailable")
	endif()

	# Same story for osgx.platform's createEGLWindow()/createGBMWindow() -- OSGX_EGL/OSGX_GBM ride
	# along from osgx::osgx the same way OSGX_IMGUI does above.
	if(NOT OSGX_WITH_EGL)
		message(STATUS "OSGX_WITH_EGL is OFF; osgx.platform.createEGLWindow will be unavailable")
	endif()

	if(NOT OSGX_WITH_GBM)
		message(STATUS "OSGX_WITH_GBM is OFF; osgx.platform.createGBMWindow will be unavailable")
	endif()
endif()
