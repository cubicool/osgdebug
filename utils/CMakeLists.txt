set(OSGDEBUG_UTILS
	"describe"
)

foreach(util IN LISTS OSGDEBUG_UTILS)
	osgdebug_compile("${util}")
endforeach()
