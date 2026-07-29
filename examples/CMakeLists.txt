set(OSGX_EXAMPLES
	"array"
	"callback"
	"culling"
	"eventhandler"
	"grid"
	"hover"
	"ibl"
	"manipulator"
	"notifyfilter"
	"picking"
	"viewer"
	"visitor"
)

foreach(example IN LISTS OSGX_EXAMPLES)
	osgx_add_executable("osgx" "${example}")
endforeach()

find_package(OpenGL REQUIRED)
target_link_libraries(osgx-hover PRIVATE OpenGL::GL)
target_link_libraries(osgx-picking PRIVATE OpenGL::GL)

find_package(PkgConfig)

if(PkgConfig_FOUND)
	pkg_check_modules(IMGUI imgui)
endif()

if(IMGUI_FOUND)
	add_executable(osgx-imgui
		"osgx-imgui.cpp"
		"../osgx/osgx.hpp"
	)

	osgx_compile_options(osgx-imgui)
	target_compile_definitions(osgx-imgui PRIVATE OSGDEBUG_IMGUI)
	target_include_directories(osgx-imgui PRIVATE ${IMGUI_INCLUDE_DIRS})
	target_link_libraries(osgx-imgui PRIVATE osgx::osgx ${IMGUI_LINK_LIBRARIES} OpenGL::GL)

	add_executable(osgx-imgui-external
		"osgx-imgui-external.cpp"
		"../osgx/osgx.hpp"
	)

	osgx_compile_options(osgx-imgui-external)
	target_compile_definitions(osgx-imgui-external PRIVATE OSGDEBUG_IMGUI)
	target_include_directories(osgx-imgui-external PRIVATE ${IMGUI_INCLUDE_DIRS})
	target_link_libraries(osgx-imgui-external PRIVATE osgx::osgx ${IMGUI_LINK_LIBRARIES} OpenGL::GL)
else()
	message(STATUS "ImGui not found via pkg-config; skipping osgx-imgui")
endif()
