set(OSGDEBUG_EXAMPLES
	"eventhandler"
	# "array"
	# "callback"
	# "culling"
	## "notifyfilter"
	"picking"
	"rtt-image"
	"viewer"
	# "visitor"
)

foreach(example IN LISTS OSGDEBUG_EXAMPLES)
	osgdebug_add_executable("${example}")
endforeach()

find_package(OpenGL REQUIRED)
target_link_libraries(osgdebug-picking PRIVATE OpenGL::GL)
