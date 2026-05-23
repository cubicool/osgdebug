set(OSGDEBUG_EXAMPLES
	"eventhandler"
	# "array"
	# "callback"
	# "culling"
	## "notifyfilter"
	"viewer"
	# "visitor"
)

foreach(example IN LISTS OSGDEBUG_EXAMPLES)
	osgdebug_add_executable("${example}")
endforeach()
