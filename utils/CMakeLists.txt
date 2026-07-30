set(OSGX_UTILS
	"describe"
	"drawables"
)

foreach(util IN LISTS OSGX_UTILS)
	osgx_add_executable("osgx" "${util}")
endforeach()

if(OSGX_BUILD_GLTF)
	# glTF viewer: needs the tinygltf headers directly (Khronos-camera/IBL-manifest decoding) plus
	# the osgx::gltf_pbribl adapter, not just the base osgx::gltf reader.
	add_executable(osgx-gltf-viewer
		"osgx-gltf-viewer.cpp"
	)

	osgx_compile_options(osgx-gltf-viewer)
	target_include_directories(osgx-gltf-viewer PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
	target_link_libraries(osgx-gltf-viewer PRIVATE osgx::gltf_pbribl)

	# GPU IBL baker: HDR equirectangular image -> GGX-prefiltered KTX2 cubemap. Generic osgx::osgx
	# baking machinery only -- no glTF-specific code.
	add_executable(osgx-gltf-iblbake-gpu
		"osgx-gltf-iblbake-gpu.cpp"
	)

	osgx_compile_options(osgx-gltf-iblbake-gpu)
	target_link_libraries(osgx-gltf-iblbake-gpu PRIVATE osgx::osgx)

	if(OSGX_BUILD_KTX2)
		# CPU-only IBL baker. Uses stb_image, osgx::ktx2, and OpenMP when available -- deliberately
		# does not link osgx::osgx, since it needs no OpenGL context at all.
		find_package(OpenMP)

		add_executable(osgx-gltf-iblbake-cpu
			"osgx-gltf-iblbake-cpu.cpp"
		)

		osgx_compile_options(osgx-gltf-iblbake-cpu)
		target_include_directories(osgx-gltf-iblbake-cpu PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
		target_link_libraries(osgx-gltf-iblbake-cpu PRIVATE osgx::ktx2)

		if(OpenMP_CXX_FOUND)
			target_link_libraries(osgx-gltf-iblbake-cpu PRIVATE OpenMP::OpenMP_CXX)
		endif()
	else()
		message(STATUS "OSGX_BUILD_KTX2 is OFF; skipping osgx-gltf-iblbake-cpu")
	endif()
else()
	message(STATUS "OSGX_BUILD_GLTF is OFF; skipping osgx-gltf-viewer/osgx-gltf-iblbake-*")
endif()
