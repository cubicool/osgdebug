set(OSGDEBUG_EXAMPLES
	"callback"
	# "viewer"
	"visitor"
	# "culling"
)

foreach(example IN LISTS OSGDEBUG_EXAMPLES)
	add_executable("osgdebug-${example}" "osgdebug-${example}.cpp" "../osgDebug.hpp")

	# target_compile_features(osgDebug INTERFACE cxx_std_20 REQUIRED)
	target_compile_features("osgdebug-${example}" PUBLIC cxx_std_20)
	target_link_libraries("osgdebug-${example}" PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
	target_include_directories("osgdebug-${example}" PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})

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

		# target_compile_options(example PRIVATE -DCHRONODE_ENABLED)

	else(WIN32)
		# target_compile_options(osgDebug PRIVATE -W -Wall -Wno-unused-parameter)
		# -fcoroutines)
	endif()
endforeach()
