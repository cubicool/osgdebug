cmake_minimum_required(VERSION 3.19)

project(osgDebug)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(OSGX_IS_TOP_LEVEL ON)
else()
	set(OSGX_IS_TOP_LEVEL OFF)
endif()

option(OSGX_BUILD_EXAMPLES "Build the osgx and osgDebug examples" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_UTILS "Build the osgx and osgDebug utilities" ${OSGX_IS_TOP_LEVEL})
option(OSGX_BUILD_PYTHON "Build the osgx and osgDebug Python modules" ${OSGX_IS_TOP_LEVEL})
option(OSGX_INSTALL "Generate osgx and osgDebug installation rules" ${OSGX_IS_TOP_LEVEL})

if(WIN32)
	set(OSGDEBUG_WARNING_OPTIONS
		/W4
		/WX
		/wd4100
		/analyze
	)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	set(OSGDEBUG_WARNING_OPTIONS
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
	set(OSGDEBUG_WARNING_OPTIONS)
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

add_library(osgx_core INTERFACE)
add_library(osgx::core ALIAS osgx_core)

target_compile_features(osgx_core INTERFACE cxx_std_20)
target_include_directories(osgx_core INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	"$<BUILD_INTERFACE:${OPENSCENEGRAPH_INCLUDE_DIRS}>"
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(osgx_core INTERFACE "$<BUILD_INTERFACE:${OPENSCENEGRAPH_LIBRARIES}>")
target_sources(osgx_core INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Warnings.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Core.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Array.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Callbacks.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Shader.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Visitors.hpp>"
)
set_target_properties(osgx_core PROPERTIES EXPORT_NAME core)

add_library(osgx
	src/GGXPrefilter.cpp
	src/LambertianBake.cpp
)
add_library(osgx::osgx ALIAS osgx)

target_compile_features(osgx PUBLIC cxx_std_20)
target_link_libraries(osgx
	PUBLIC osgx::core
	PRIVATE OpenGL::GL
)
target_sources(osgx PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Grid.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/IBL.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/GGXPrefilter.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/LambertianBake.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Manipulators.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/PBR.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgx/Picking.hpp>"
)
set_target_properties(osgx PROPERTIES
	EXPORT_NAME osgx
	POSITION_INDEPENDENT_CODE ON
)

target_compile_options(osgx PRIVATE ${OSGDEBUG_WARNING_OPTIONS})

add_library(osgDebug INTERFACE)
add_library(osgDebug::osgDebug ALIAS osgDebug)

target_link_libraries(osgDebug INTERFACE osgx::osgx)
target_sources(osgDebug INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgDebug.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgDebug/Debug.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osgDebug/ImGui.hpp>"
)
set_target_properties(osgDebug PROPERTIES EXPORT_NAME osgDebug)

if(OSGX_INSTALL)
	install(TARGETS osgx_core osgx EXPORT osgxTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	)
	install(DIRECTORY osgx/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgx"
		FILES_MATCHING PATTERN "*.hpp"
	)
	install(FILES osgx.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
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

	install(TARGETS osgDebug EXPORT osgDebugTargets)
	install(DIRECTORY osgDebug/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/osgDebug"
		FILES_MATCHING PATTERN "*.hpp"
	)
	install(FILES osgDebug.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
	install(EXPORT osgDebugTargets
		FILE osgDebugTargets.cmake
		NAMESPACE osgDebug::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgDebug"
	)

	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/osgDebugConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/osgDebugConfig.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgDebug"
	)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/osgDebugConfig.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osgDebug"
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

function(OSGDEBUG_COMPILE_OPTIONS _target)
	target_compile_features("${_target}" PUBLIC cxx_std_20)
	target_link_libraries("${_target}" PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
	target_include_directories("${_target}" PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})
	target_compile_options("${_target}" PRIVATE ${OSGDEBUG_WARNING_OPTIONS})
endfunction()

function(OSGDEBUG_ADD_EXECUTABLE _prefix_or_topic)
	if(ARGC EQUAL 1)
		set(_target "osgdebug-${_prefix_or_topic}")
		set(_library osgDebug::osgDebug)
	else()
		set(_target "${_prefix_or_topic}-${ARGV1}")

		if(_prefix_or_topic STREQUAL "osgx")
			set(_library osgx::osgx)
		else()
			set(_library osgDebug::osgDebug)
		endif()
	endif()

	add_executable("${_target}"
		"${_target}.cpp"
		"../osgx.hpp"
		"../osgDebug.hpp"
	)

	osgdebug_compile_options("${_target}")
	target_link_libraries("${_target}" PRIVATE "${_library}")
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
	osgdebug_compile_options(osgx_python)
	set_target_properties(osgx_python PROPERTIES POSITION_INDEPENDENT_CODE ON)

	pybind11_add_module(osgDebug_python "ext/osgDebug-python.cpp")

	set_target_properties(osgDebug_python PROPERTIES OUTPUT_NAME "osgDebug")
	target_include_directories(osgDebug_python PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		${OPENSCENEGRAPH_INCLUDE_DIRS}
		"/home/cubicool/dev/OpenSceneGraph.py"
		"/home/cubicool/dev/OpenSceneGraph.py/etc"
	)
	target_link_libraries(osgDebug_python PRIVATE
		osgDebug::osgDebug
		pybind11::module
	)
	osgdebug_compile_options(osgDebug_python)
	set_target_properties(osgDebug_python PROPERTIES POSITION_INDEPENDENT_CODE ON)

	# Same ImGui detection as examples/CMakeLists.txt (pkg_check_modules is
	# subdirectory-scoped, so it has to be repeated here rather than reused).
	find_package(PkgConfig)

	if(PkgConfig_FOUND)
		pkg_check_modules(IMGUI imgui)
	endif()

	if(IMGUI_FOUND)
		find_package(OpenGL REQUIRED)
		target_compile_definitions(osgDebug_python PRIVATE OSGDEBUG_IMGUI)
		target_include_directories(osgDebug_python PRIVATE ${IMGUI_INCLUDE_DIRS})
		target_link_libraries(osgDebug_python PRIVATE ${IMGUI_LINK_LIBRARIES} OpenGL::GL)
	else()
		message(STATUS "ImGui not found via pkg-config; osgDebug.imgui Python bindings will be unavailable")
	endif()
endif()
