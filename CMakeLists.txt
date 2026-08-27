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

# osgx's Python bindings reuse OpenSceneGraph.py's vendored pybind11 (etc/pybind11) and its
# pyosg/pybind11x headers rather than vendoring another copy -- default assumes sibling checkouts
# under a common top-level dev directory (.../dev/osgx, .../dev/OpenSceneGraph.py); override with
# -DOSGX_OPENSCENEGRAPH_PY_DIR=/path/to/OpenSceneGraph.py if that doesn't hold.
set(OSGX_OPENSCENEGRAPH_PY_DIR "${CMAKE_SOURCE_DIR}/../OpenSceneGraph.py" CACHE PATH
	"Path to an OpenSceneGraph.py checkout (used for its vendored ext/pybind11 and pyosg/pybind11x headers)"
)
option(OSGX_BUILD_KTX2 "Build the KTX2 osgDB plugin (osgdb_ktx2) and the osgx::ktx2 static library" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_GLTF "Build the glTF osgDB plugin (osgdb_gltf) and the osgx::gltf/osgx::gltf_pbribl static libraries" ${OSGX_IS_TOP_LEVEL})
option(OSGX_INSTALL "Generate osgx installation rules" ${OSGX_IS_TOP_LEVEL})

if(WIN32)
	set(OSGX_WARNING_OPTIONS
		/W4
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

# Standalone osgx discovers an installed OSG in the usual way. When it is
# embedded below a project that has already added OSG as a subdirectory (for
# example OpenSceneGraph.py's pinned FetchContent build), use those targets
# directly instead: an installation/package config must not be required.
set(OSGX_OSG_COMPONENTS
	OpenThreads
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

if(TARGET osg)
	set(OPENSCENEGRAPH_LIBRARIES ${OSGX_OSG_COMPONENTS})
	set(OPENSCENEGRAPH_INCLUDE_DIRS
		"${OpenSceneGraph_SOURCE_DIR}/include"
		"${OpenSceneGraph_BINARY_DIR}/include"
	)
else()
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
endif()
find_package(OpenGL REQUIRED)

# The current platform layer is specifically Linux/X11.  Keep it separate from
# osgx's portable rendering helpers so a Windows build does not acquire a
# pkg-config or X11 dependency merely by enabling osgx.
set(OSGX_PLATFORM_SUPPORTED OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(OSGX_PLATFORM_SUPPORTED ON)
endif()

option(OSGX_WITH_PLATFORM
	"Compile the Linux/X11 osgx::platform helpers"
	${OSGX_PLATFORM_SUPPORTED}
)

if(OSGX_WITH_PLATFORM AND NOT OSGX_PLATFORM_SUPPORTED)
	message(FATAL_ERROR
		"OSGX_WITH_PLATFORM is currently supported only on Linux; "
		"disable it to build osgx's portable functionality on this platform."
	)
endif()

# pkg-config currently supplies the optional Linux integrations.  On other
# platforms deliberately leave them unavailable rather than making pkg-config
# a hard requirement for the portable library and Python module.
if(OSGX_PLATFORM_SUPPORTED)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(IMGUI imgui)
	pkg_check_modules(EGL egl)
	pkg_check_modules(GBM gbm libdrm)

	if(OSGX_WITH_PLATFORM)
		pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xrandr)
	endif()
else()
	set(IMGUI_FOUND OFF)
	set(EGL_FOUND OFF)
	set(GBM_FOUND OFF)
endif()

option(OSGX_WITH_IMGUI "Compile osgx::imgui (Dear ImGui overlay) into libosgx" ${IMGUI_FOUND})

if(OSGX_WITH_IMGUI AND NOT IMGUI_FOUND)
	message(FATAL_ERROR "OSGX_WITH_IMGUI is ON but Dear ImGui was not found via pkg-config")
endif()

# GraphicsWindowEGL/GBM are Linux-only extensions of osgx::platform. They are
# deliberately unavailable whenever the platform layer is unavailable.
option(OSGX_WITH_EGL "Compile osgx::platform's EGL-backed GraphicsWindowEGL into libosgx" ${EGL_FOUND})
option(OSGX_WITH_GBM "Compile osgx::platform's GBM/DRM-backed GraphicsWindowGBM into libosgx" ${GBM_FOUND})

if(OSGX_WITH_EGL AND NOT EGL_FOUND)
	message(FATAL_ERROR "OSGX_WITH_EGL is ON but EGL was not found via pkg-config")
endif()

if(OSGX_WITH_GBM AND NOT GBM_FOUND)
	message(FATAL_ERROR "OSGX_WITH_GBM is ON but GBM/libdrm was not found via pkg-config")
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Version.hpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp"
	@ONLY
)

add_library(osgx_core INTERFACE)
add_library(osgx::core ALIAS osgx_core)

target_compile_features(osgx_core INTERFACE cxx_std_20)
target_include_directories(osgx_core INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
	"$<BUILD_INTERFACE:${OPENSCENEGRAPH_INCLUDE_DIRS}>"
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(osgx_core INTERFACE ${OPENSCENEGRAPH_LIBRARIES})
target_sources(osgx_core INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Warnings.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/osgx/Version.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Core.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Array.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Callbacks.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Visitors.hpp>"
)
set_target_properties(osgx_core PROPERTIES EXPORT_NAME core)

# osgx is an implementation dependency of the Python extension.  With MSVC,
# keeping it as a DLL would require every public class (including its vtables,
# RTTI, and data) to carry a dllimport/dllexport annotation.  Link it statically
# into its in-tree consumers instead; Linux retains the normal shared library.
if(MSVC)
	set(OSGX_LIBRARY_TYPE STATIC)
else()
	set(OSGX_LIBRARY_TYPE SHARED)
endif()

add_library(osgx ${OSGX_LIBRARY_TYPE}
	src/Array.cpp
	src/CameraIntents.cpp
	src/CaptureCubeMap.cpp
	src/Debug.cpp
	src/GBuffer.cpp
	src/GGXPrefilter.cpp
	src/Gizmos.cpp
	src/Grid.cpp
	src/IBL.cpp
	src/LambertianBake.cpp
	src/Manipulators.cpp
	src/PBR.cpp
	src/Picking.cpp
	src/PixelText.cpp
	src/Shadow.cpp
	src/Shapes.cpp
	src/Shader.cpp
)
add_library(osgx::osgx ALIAS osgx)

target_compile_features(osgx PUBLIC cxx_std_20)
target_link_libraries(osgx
	PUBLIC osgx::core
	PRIVATE OpenGL::GL
)
target_sources(osgx PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/osgx.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Gizmos.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Grid.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/IBL.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/CaptureCubeMap.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/GBuffer.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/GGXPrefilter.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/LambertianBake.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Manipulators.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/PBR.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Picking.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/PixelText.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Shadow.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Shapes.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Shader.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Debug.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/ImGui.hpp>"
)
set_target_properties(osgx PROPERTIES
	EXPORT_NAME osgx
	POSITION_INDEPENDENT_CODE ON
)

# The Python extension is also named "osgx" and MSVC emits an import library
# beside it.  When osgx itself is static (the MSVC configuration above), both
# would otherwise claim Release/osgx.lib.  Keep the logical CMake target name
# stable while giving its implementation archive a distinct filename.
if(MSVC)
	set_target_properties(osgx PROPERTIES ARCHIVE_OUTPUT_NAME osgx_static)
endif()

target_compile_options(osgx PRIVATE ${OSGX_WARNING_OPTIONS})

if(OSGX_WITH_PLATFORM)
	target_sources(osgx PRIVATE
		src/Cursor.cpp
		src/Linux.cpp
	)
	target_sources(osgx PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Linux.hpp>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/GraphicsWindowEGL.hpp>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/GraphicsWindowGBM.hpp>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/osgx/Cursor.hpp>"
	)
	target_compile_definitions(osgx PUBLIC OSGX_PLATFORM)
	target_link_libraries(osgx PRIVATE PkgConfig::X11)
endif()

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
	install(DIRECTORY src/osgx/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgx"
		FILES_MATCHING
		PATTERN "*.hpp"
		PATTERN "Linux.hpp" EXCLUDE
		PATTERN "GraphicsWindowEGL.hpp" EXCLUDE
		PATTERN "GraphicsWindowGBM.hpp" EXCLUDE
		PATTERN "Cursor.hpp" EXCLUDE
	)
	if(OSGX_WITH_PLATFORM)
		install(FILES
			src/osgx/Linux.hpp
			src/osgx/GraphicsWindowEGL.hpp
			src/osgx/GraphicsWindowGBM.hpp
			src/osgx/Cursor.hpp
			DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgx"
		)
	endif()
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
		"${PROJECT_SOURCE_DIR}/src"
		"${PROJECT_BINARY_DIR}"
	)
	target_include_directories("${_target}" PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})
	target_compile_options("${_target}" PRIVATE ${OSGX_WARNING_OPTIONS})
endfunction()

function(OSGX_ADD_EXECUTABLE _prefix _topic)
	set(_target "${_prefix}-${_topic}")

	add_executable("${_target}"
		"${_target}.cpp"
		"../src/osgx/osgx.hpp"
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

if(OSGX_BUILD_PYTHON AND Python3_Development.Module_FOUND)
	if(NOT EXISTS "${OSGX_OPENSCENEGRAPH_PY_DIR}/etc/pybind11/CMakeLists.txt")
		message(FATAL_ERROR
			"OSGX_BUILD_PYTHON is ON, but OSGX_OPENSCENEGRAPH_PY_DIR "
			"(${OSGX_OPENSCENEGRAPH_PY_DIR}) doesn't contain etc/pybind11 -- checkout "
			"OpenSceneGraph.py next to osgx, pass -DOSGX_OPENSCENEGRAPH_PY_DIR=/path/to/"
			"OpenSceneGraph.py, or disable -DOSGX_BUILD_PYTHON=OFF."
		)
	endif()

	# An embedding parent may already provide this exact vendored pybind11
	# target. Reusing it avoids defining the targets a second time.
	if(NOT TARGET pybind11::module)
		add_subdirectory("${OSGX_OPENSCENEGRAPH_PY_DIR}/etc/pybind11" "ext/pybind11")
	endif()

	# The bindings are split across ext/python/osgx-*.cpp -- one translation unit per osgx::
	# submodule (see ext/python/osgx-python.hpp for the bind_*() declarations) -- instead of one
	# ~1700-line monolithic ext/osgx-python.cpp. osgx_python_bindings is a plain STATIC lib, not
	# itself a Python module: touching one submodule's bindings only recompiles that one file plus
	# a relink, a from-scratch build parallelizes across `make -j#`, and pybind11_add_module below
	# only ever has to compile the thin ext/python/osgx.cpp PYBIND11_MODULE entry point.
	add_library(osgx_python_bindings STATIC
		"ext/python/osgx-core.cpp"
		"ext/python/osgx-callbacks.cpp"
		"ext/python/osgx-pbr.cpp"
		"ext/python/osgx-shadow.cpp"
		"ext/python/osgx-gbuffer.cpp"
		"ext/python/osgx-ibl.cpp"
		"ext/python/osgx-debug.cpp"
		"ext/python/osgx-picking.cpp"
		"ext/python/osgx-shapes.cpp"
		"ext/python/osgx-gizmos.cpp"
		"ext/python/osgx-pixel-text.cpp"
	)

	# PUBLIC, not PRIVATE: ext/python/osgx.cpp (compiled into the separate osgx_python target
	# below, which links this one) also includes osgx-python.hpp and therefore needs every one of
	# these same include dirs -- PRIVATE here would resolve fine for this target's own sources but
	# leave osgx.cpp's #include "osgx/Version.hpp" and osgx-python.hpp's
	# #include "pyosg/pyosg.hpp"/"pybind11x.hpp" unable to find anything.
	target_include_directories(osgx_python_bindings PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		${OPENSCENEGRAPH_INCLUDE_DIRS}
		"${OSGX_OPENSCENEGRAPH_PY_DIR}"
		"${OSGX_OPENSCENEGRAPH_PY_DIR}/etc"
	)
	# PUBLIC, not PRIVATE: osgx::osgx's OSGX_IMGUI/OSGX_EGL/OSGX_GBM compile definitions need to
	# keep propagating all the way to the osgx_python target below (specifically ext/python/
	# osgx.cpp's own #ifdef OSGX_IMGUI guard around the imgui submodule), not just stop here.
	target_link_libraries(osgx_python_bindings PUBLIC
		osgx::osgx
		pybind11::module
	)
	osgx_compile_options(osgx_python_bindings)
	# MSVC's /analyze reports C28182 from pybind11's dispatcher template even
	# when the corresponding runtime path proves the pointer non-null.  This is
	# an analyzer false positive at template-instantiation time; keep /analyze
	# enabled and suppress only that diagnostic for the Python bindings.
	if(MSVC)
		target_compile_options(osgx_python_bindings PRIVATE /wd28182)
	endif()
	set_target_properties(osgx_python_bindings PROPERTIES POSITION_INDEPENDENT_CODE ON)

	if(OSGX_WITH_PLATFORM)
		target_sources(osgx_python_bindings PRIVATE "ext/python/osgx-platform.cpp")
	else()
		message(STATUS "OSGX_WITH_PLATFORM is OFF; osgx.platform will be unavailable")
	endif()

	# osgx.imgui Python bindings ride along automatically: osgx_python_bindings links osgx::osgx,
	# which already carries OSGX_IMGUI + the Dear ImGui include dirs and link libraries as PUBLIC
	# usage requirements when OSGX_WITH_IMGUI is on. ext/python/osgx-imgui.cpp itself is a no-op
	# (guarded out entirely) without that define, so only bother compiling it when it's on.
	if(OSGX_WITH_IMGUI)
		target_sources(osgx_python_bindings PRIVATE "ext/python/osgx-imgui.cpp")
	else()
		message(STATUS "OSGX_WITH_IMGUI is OFF; osgx.imgui Python bindings will be unavailable")
	endif()

	# OSGX_EGL/OSGX_GBM ride along from osgx::osgx the same way OSGX_IMGUI
	# does above; ext/python/osgx-platform.cpp guards those two bindings
	# internally with #ifdef OSGX_EGL/OSGX_GBM.
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

	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		set_source_files_properties("ext/python/osgx.cpp" PROPERTIES
			COMPILE_OPTIONS "-w"
		)
	endif()
endif()
