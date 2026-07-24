cmake_minimum_required(VERSION 3.19)

project(osgDebug)

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

find_package(Python3 COMPONENTS Interpreter Development OPTIONAL)
# add_subdirectory("/home/cubicool/dev/slughorn/ext/pybind11" "ext/pybind11")

if(isMultiConfig)
	if(NOT "Asan" IN_LIST CMAKE_CONFIGURATION_TYPES)
		list(APPEND CMAKE_CONFIGURATION_TYPES Asan)
	endif()

else()
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

	if(WIN32)
		# target_compile_options(osgDebug PRIVATE /W4 /wd4100)
		# /Wall /wd4225 /wd4668) # /wd4244 /wd4146 /wd4267 /wd4996 /wd4334 /wd4311)

		# #!/usr/bin/env bash
		#
		# pushd ~/local/bin
		# cp ~/dev/Quick/osg/BUILD-wsl/Debug/osg.exe .
		# ./osg.exe
		# popd

		# add_custom_target(osgDebug_sh
		# 	COMMAND echo "${F}" > .osgDebug
		# 	COMMENT "Generating WSL wrapper script"
		# )

		# target_compile_options(_target PRIVATE -DCHRONODE_ENABLED)

	else(WIN32)
		target_compile_options("${_target}" PRIVATE
			-W
			-Wall
			-Wshadow
			-Wconversion
			-Wpedantic
			-Wno-unused-parameter
		)
	endif()
endfunction()

function(OSGDEBUG_ADD_EXECUTABLE _prefix_or_topic)
	if(ARGC EQUAL 1)
		set(_target "osgdebug-${_prefix_or_topic}")
	else()
		set(_target "${_prefix_or_topic}-${ARGV1}")
	endif()

	add_executable("${_target}"
		"${_target}.cpp"
		"../osgx.hpp"
		"../osgDebug.hpp"
	)

	osgdebug_compile_options("${_target}")
endfunction()

add_subdirectory("examples")
add_subdirectory("utils")
# add_subdirectory("plugins") # TODO: Disabled

if(Python3_Development_FOUND)
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
		pybind11::module
		${OPENSCENEGRAPH_LIBRARIES}
	)
	target_compile_features(osgx_python PUBLIC cxx_std_20)
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
		pybind11::module
		${OPENSCENEGRAPH_LIBRARIES}
	)
	target_compile_features(osgDebug_python PUBLIC cxx_std_20)
	# target_compile_options(osgDebug_python PRIVATE ${SLUGHORN_COMPILE_OPTIONS})
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
