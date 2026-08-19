add_library(osgx_gltf STATIC
	Accessor.cpp
	Animation.cpp
	Material.cpp
	Mesh.cpp
	Reader.cpp
	ReaderImpl.cpp
	Scene.cpp
	Skin.cpp
	Texture.cpp
	TinyGLTF.cpp
)
add_library(osgx::gltf ALIAS osgx_gltf)

osgx_compile_options(osgx_gltf)
target_include_directories(osgx_gltf PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
target_link_libraries(osgx_gltf PUBLIC osgx::core)
target_sources(osgx_gltf PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../osgx/gltf/Reader.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../osgx/gltf/Shader.hpp>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../osgx/gltf/SimplePlayer.hpp>"
)
set_target_properties(osgx_gltf PROPERTIES
	EXPORT_NAME gltf
	POSITION_INDEPENDENT_CODE ON
)

add_library(osgx_gltf_pbribl STATIC
	PBRIBL.cpp
)
add_library(osgx::gltf_pbribl ALIAS osgx_gltf_pbribl)

osgx_compile_options(osgx_gltf_pbribl)
target_include_directories(osgx_gltf_pbribl PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
target_link_libraries(osgx_gltf_pbribl PUBLIC osgx::gltf osgx::osgx)
target_sources(osgx_gltf_pbribl PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../osgx/gltf/PBRIBL.hpp>"
)
set_target_properties(osgx_gltf_pbribl PROPERTIES
	EXPORT_NAME gltf_pbribl
	POSITION_INDEPENDENT_CODE ON
)

# tinygltf's bundled JSON header (json.hpp) uses the deprecated std::is_pod trait; isolate that
# third-party warning to just these translation units, same treatment used before the merge.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(
		Accessor.cpp Animation.cpp Material.cpp Mesh.cpp PBRIBL.cpp Reader.cpp ReaderImpl.cpp Scene.cpp Skin.cpp Texture.cpp TinyGLTF.cpp
		PROPERTIES
		COMPILE_OPTIONS "-Wno-deprecated-declarations"
	)
	# nlohmann/json 3.5.0, bundled by this pinned tinygltf version, includes <ciso646>.
	# GCC 15 emits a preprocessor warning for that header in C++20 mode; keep warnings-as-errors
	# enabled for project code while isolating the third-party compatibility warning here.
	set_property(SOURCE TinyGLTF.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-cpp")
	# GCC 14's stringop-overflow analysis cannot prove the bounds in stb_image's PNG parser.
	# This is vendored third-party code, so keep -Werror for the rest of osgx and suppress only
	# the false positive on the translation unit that includes stb_image's implementation.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set_property(SOURCE TinyGLTF.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-stringop-overflow")
	endif()
endif()

if(OSGX_INSTALL)
	install(TARGETS osgx_gltf osgx_gltf_pbribl EXPORT osgxTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	)
endif()
