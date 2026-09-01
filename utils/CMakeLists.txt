set(OSGX_UTILS
	"describe"
	"drawables"
)

foreach(util IN LISTS OSGX_UTILS)
	osgx_add_executable("osgx" "${util}")
endforeach()

if(OSGX_BUILD_KTX2)
	osgx_add_executable("osgx" "cubemap")
else()
	message(STATUS "OSGX_BUILD_KTX2 is OFF; skipping osgx-cubemap")
endif()

if(OSGX_BUILD_GLTF)
	# glTF viewer: needs the tinygltf headers directly (Khronos-camera/IBL-manifest decoding) plus
	# the osgx::gltf_pbribl adapter, not just the base osgx::gltf reader.
	add_executable(osgx-gltf-viewer
		"osgx-gltf-viewer.cpp"
	)

	osgx_compile_options(osgx-gltf-viewer)
	target_include_directories(osgx-gltf-viewer PRIVATE "${PROJECT_SOURCE_DIR}/ext/tinygltf")
	target_link_libraries(osgx-gltf-viewer PRIVATE osgx::gltf_pbribl)

	# HDR equirectangular image -> complete static PBR/IBL environment bundle: two cubemaps, a
	# BRDF LUT, and a minimal osgx_pbribl glTF manifest consumable by osgx-gltf-viewer --env.
	add_executable(osgx-pbribl
		"osgx-pbribl.cpp"
	)

	osgx_compile_options(osgx-pbribl)
	target_link_libraries(osgx-pbribl PRIVATE osgx::osgx)

	if(OSGX_BUILD_KTX2)
		find_package(OpenMP)

		target_link_libraries(osgx-pbribl PRIVATE osgx::ktx2)
		target_compile_definitions(osgx-pbribl PRIVATE OSGX_PBRIBL_SOFTWARE_AVAILABLE=1)

		if(OpenMP_CXX_FOUND)
			target_link_libraries(osgx-pbribl PRIVATE OpenMP::OpenMP_CXX)
		endif()
	else()
		message(STATUS "OSGX_BUILD_KTX2 is OFF; osgx-pbribl --software will be unavailable")
	endif()

else()
	message(STATUS "OSGX_BUILD_GLTF is OFF; skipping osgx-gltf-viewer/osgx-pbribl")
endif()
