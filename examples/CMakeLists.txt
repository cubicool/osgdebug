set(OSGDEBUG_EXAMPLES
	"eventhandler"
	"array"
	"callback"
	# TOOD: Fix this!
	# "culling"
	"notifyfilter"
	"hover"
	"manipulator"
	"picking"
	"viewer"
	"visitor"
)

foreach(example IN LISTS OSGDEBUG_EXAMPLES)
	osgdebug_add_executable("${example}")
endforeach()

find_package(OpenGL REQUIRED)
target_link_libraries(osgdebug-hover PRIVATE OpenGL::GL)
target_link_libraries(osgdebug-picking PRIVATE OpenGL::GL)

find_package(PkgConfig)

if(PkgConfig_FOUND)
    pkg_check_modules(IMGUI imgui)
endif()

if(IMGUI_FOUND)
    add_executable(osgdebug-imgui
        "osgdebug-imgui.cpp"
        "../osgx.hpp"
        "../osgDebug.hpp"
        "../osgDebugImGui.hpp"
    )

    osgdebug_compile_options(osgdebug-imgui)
    target_include_directories(osgdebug-imgui PRIVATE ${IMGUI_INCLUDE_DIRS})
    target_link_libraries(osgdebug-imgui PRIVATE ${IMGUI_LINK_LIBRARIES} OpenGL::GL)
else()
    message(STATUS "ImGui not found via pkg-config; skipping osgdebug-imgui")
endif()
