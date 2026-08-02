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
option(OSGX_BUILD_KTX2 "Build the KTX2 osgDB plugin (osgdb_ktx2) and the osgx::ktx2 static library" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_GLTF "Build the glTF osgDB plugin (osgdb_gltf) and the osgx::gltf/osgx::gltf_pbribl static libraries" ${OSGX_IS_TOP_LEVEL})
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
target_link_libraries(osgx_core INTERFACE ${OPENSCENEGRAPH_LIBRARIES})
target_sources(osgx_core INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Warnings.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Core.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Array.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Callbacks.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Visitors.hpp>"
)
set_target_properties(osgx_core PROPERTIES EXPORT_NAME core)

add_library(osgx SHARED
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
	# BEFORE, not just PUBLIC: guarantees this checkout's own headers always win the #include <...>
	# search over ${OPENSCENEGRAPH_INCLUDE_DIRS} below -- which, on this machine, is frequently the
	# SAME prefix an earlier `cmake --install` put a stale copy of osgx's own headers into (OSG and
	# osgx have historically shared an install prefix). Without this, a target can silently compile
	# against a stale installed osgx/*.hpp instead of a live source-tree edit -- not just wrong
	# behavior, but a real ABI mismatch for any public struct whose fields change, since callers and
	# the freshly-compiled osgx implementation can then disagree about a type's size/layout.
	#
	# This used to only be applied in OSGX_ADD_EXECUTABLE below (examples/utils), not here -- so any
	# LIBRARY calling this function directly (osgx_gltf, osgx_gltf_pbribl, osgx_ktx2, osgdb_gltf,
	# osgdb_ktx2, osgdb_debug, osgx_python_bindings, osgx_python) was still exposed. Found the hard
	# way 2026-07-31 via GGXPrefilterOptions::fireflyClamp: osgx_gltf_pbribl silently built against
	# an installed GGXPrefilterOptions missing that field entirely while `osgx` itself (compiled
	# fresh from source, via this same function but previously without this guard) read it,
	# causing GGXPrefilterScene's bake to read past the end of the caller's undersized struct.
	#
	# PRIVATE, not PUBLIC: this only needs to win the #include <...> search for THIS target's own
	# compilation -- every real compiled target in this project calls osgx_compile_options() on
	# itself directly, so there's no need to propagate it to downstream consumers too. PUBLIC would
	# also break `install(EXPORT osgxTargets ...)` for the library targets that use it (osgx_gltf,
	# osgx_gltf_pbribl, osgx_ktx2, ...): CMake refuses to export a raw, unwrapped absolute
	# source/build-tree path in INTERFACE_INCLUDE_DIRECTORIES, since it would be meaningless for an
	# external consumer installing to a different prefix (confirmed by hitting exactly that "which
	# is prefixed in the source directory" configure error on the first attempt at this fix).
	target_include_directories("${_target}" BEFORE PRIVATE
		"${PROJECT_SOURCE_DIR}"
		"${PROJECT_BINARY_DIR}"
	)
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
endfunction()

# ---------------------------------------------------------------------------
# KTX-Software (ext/KTX-Software submodule)
# Disable everything we don't need to keep the build fast.
# ---------------------------------------------------------------------------
if(OSGX_BUILD_KTX2)
	set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
	set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)
	add_subdirectory("ext/KTX-Software")
endif()

if(OSGX_BUILD_GLTF)
	add_subdirectory("src/gltf")
endif()

add_subdirectory("plugins")

# examples/ and utils/ must come after the blocks above -- some utils (the osgx::gltf tools) link
# osgx::gltf_pbribl/osgx::ktx2, which are only defined by that point in the configure pass.
if(OSGX_BUILD_EXAMPLES)
	add_subdirectory("examples")
endif()

if(OSGX_BUILD_UTILS)
	add_subdirectory("utils")
endif()

if(OSGX_BUILD_PYTHON AND Python3_Development_FOUND)
	# TODO: Make this an option too!
	add_subdirectory("/home/cubicool/dev/OpenSceneGraph.py/etc/pybind11" "ext/pybind11")

	# The bindings are split across ext/python/osgx-*.cpp -- one translation unit per osgx::
	# submodule (see ext/python/osgx-python.hpp for the bind_*() declarations) -- instead of one
	# ~1700-line monolithic ext/osgx-python.cpp. osgx_python_bindings is a plain STATIC lib, not
	# itself a Python module: touching one submodule's bindings only recompiles that one file plus
	# a relink, a from-scratch build parallelizes across `make -j#`, and pybind11_add_module below
	# only ever has to compile the thin ext/python/osgx.cpp PYBIND11_MODULE entry point.
	add_library(osgx_python_bindings STATIC
		"ext/python/osgx-core.cpp"
		"ext/python/osgx-pbr.cpp"
		"ext/python/osgx-ibl.cpp"
		"ext/python/osgx-debug.cpp"
		"ext/python/osgx-platform.cpp"
	)

	# PUBLIC, not PRIVATE: ext/python/osgx.cpp (compiled into the separate osgx_python target
	# below, which links this one) also includes osgx-python.hpp and therefore needs every one of
	# these same include dirs -- PRIVATE here would resolve fine for this target's own sources but
	# leave osgx.cpp's #include "osgx/osgx.hpp"/"pyosg/pyosg.hpp"/"pybind11x.hpp" unable to find
	# anything.
	target_include_directories(osgx_python_bindings PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}"
		${OPENSCENEGRAPH_INCLUDE_DIRS}
		"/home/cubicool/dev/OpenSceneGraph.py"
		"/home/cubicool/dev/OpenSceneGraph.py/etc"
	)
	# PUBLIC, not PRIVATE: osgx::osgx's OSGX_IMGUI/OSGX_EGL/OSGX_GBM compile definitions need to
	# keep propagating all the way to the osgx_python target below (specifically ext/python/
	# osgx.cpp's own #ifdef OSGX_IMGUI guard around the imgui submodule), not just stop here.
	target_link_libraries(osgx_python_bindings PUBLIC
		osgx::osgx
		pybind11::module
	)
	osgx_compile_options(osgx_python_bindings)
	set_target_properties(osgx_python_bindings PROPERTIES POSITION_INDEPENDENT_CODE ON)

	# osgx.imgui Python bindings ride along automatically: osgx_python_bindings links osgx::osgx,
	# which already carries OSGX_IMGUI + the Dear ImGui include dirs and link libraries as PUBLIC
	# usage requirements when OSGX_WITH_IMGUI is on. ext/python/osgx-imgui.cpp itself is a no-op
	# (guarded out entirely) without that define, so only bother compiling it when it's on.
	if(OSGX_WITH_IMGUI)
		target_sources(osgx_python_bindings PRIVATE "ext/python/osgx-imgui.cpp")
	else()
		message(STATUS "OSGX_WITH_IMGUI is OFF; osgx.imgui Python bindings will be unavailable")
	endif()

	# Same story for osgx.platform's createEGLWindow()/createGBMWindow() -- OSGX_EGL/OSGX_GBM ride
	# along from osgx::osgx the same way OSGX_IMGUI does above; ext/python/osgx-platform.cpp
	# guards those two bindings internally with #ifdef OSGX_EGL/OSGX_GBM.
	if(NOT OSGX_WITH_EGL)
		message(STATUS "OSGX_WITH_EGL is OFF; osgx.platform.createEGLWindow will be unavailable")
	endif()

	if(NOT OSGX_WITH_GBM)
		message(STATUS "OSGX_WITH_GBM is OFF; osgx.platform.createGBMWindow will be unavailable")
	endif()

	# osgx.gltf, unlike imgui/EGL/GBM above, isn't already carried by osgx::osgx -- it needs its
	# own explicit link (osgx::gltf_pbribl, which pulls in osgx::gltf + osgx::osgx transitively),
	# the tinygltf headers, and a preprocessor define to guard ext/python/osgx-gltf.cpp (which also
	# isn't added to the source list at all unless this is on). PUBLIC for the same reason as the
	# osgx::osgx link above: ext/python/osgx.cpp's own #ifdef OSGX_GLTF guard needs to see it too.
	if(OSGX_BUILD_GLTF)
		target_sources(osgx_python_bindings PRIVATE "ext/python/osgx-gltf.cpp")
		target_compile_definitions(osgx_python_bindings PUBLIC OSGX_GLTF)
		target_include_directories(osgx_python_bindings PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
		target_link_libraries(osgx_python_bindings PRIVATE osgx::gltf_pbribl)

		# tinygltf's bundled json.hpp uses the deprecated std::is_pod trait, same as everywhere
		# else it's included in this repo.
		if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
			set_source_files_properties("ext/python/osgx-gltf.cpp" PROPERTIES
				COMPILE_OPTIONS "-Wno-deprecated-declarations"
			)
		endif()
	else()
		message(STATUS "OSGX_BUILD_GLTF is OFF; osgx.gltf Python bindings will be unavailable")
	endif()

	pybind11_add_module(osgx_python "ext/python/osgx.cpp")

	set_target_properties(osgx_python PROPERTIES
		OUTPUT_NAME "osgx"
		POSITION_INDEPENDENT_CODE ON
	)
	target_link_libraries(osgx_python PRIVATE osgx_python_bindings)
	osgx_compile_options(osgx_python)
endif()
