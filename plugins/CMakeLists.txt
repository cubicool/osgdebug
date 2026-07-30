# osgdb_debug intentionally has no build option and is not add_subdirectory()'d here yet -- it
# doesn't have a valid design (see TODO.md), and fixing that is out of scope for the KTX2
# migration. Its CMakeLists.txt exists so the directory shape is ready, not because it builds.

if(OSGX_BUILD_KTX2)
	add_subdirectory("ktx2")
endif()

if(OSGX_BUILD_GLTF)
	add_subdirectory("gltf")
endif()
