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

function(OSGDEBUG_COMPILE _target)
	add_executable("osgdebug-${_target}" "osgdebug-${_target}.cpp" "../osgDebug.hpp")

	# target_compile_features(osgDebug INTERFACE cxx_std_20 REQUIRED)
	target_compile_features("osgdebug-${_target}" PUBLIC cxx_std_20)
	target_link_libraries("osgdebug-${_target}" PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
	target_include_directories("osgdebug-${_target}" PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})

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
		target_compile_options("osgdebug-${_target}" PRIVATE
			-W
			-Wall
			-Wshadow
			-Wconversion
			-Wpedantic
			-Wno-unused-parameter
		)
	endif()
endfunction()

add_subdirectory("examples")
add_subdirectory("utils")
