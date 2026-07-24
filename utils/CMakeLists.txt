set(OSGX_UTILS
	"describe"
)

set(OSGDEBUG_UTILS
	"drawables"
)

foreach(util IN LISTS OSGX_UTILS)
	osgdebug_add_executable("osgx" "${util}")
endforeach()

foreach(util IN LISTS OSGDEBUG_UTILS)
	osgdebug_add_executable("osgdebug" "${util}")
endforeach()
