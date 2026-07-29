set(OSGX_UTILS
	"describe"
	"drawables"
)

foreach(util IN LISTS OSGX_UTILS)
	osgx_add_executable("osgx" "${util}")
endforeach()
