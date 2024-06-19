set(OSGDEBUG_UTILS
	"describe"
	"drawables"
)

foreach(util IN LISTS OSGDEBUG_UTILS)
	osgdebug_compile("${util}")
endforeach()
