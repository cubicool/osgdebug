set(OSGDEBUG_EXAMPLES
	# "callback"
	# "culling"
	"notifyfilter"
	"viewer"
	# "visitor"
)

foreach(example IN LISTS OSGDEBUG_EXAMPLES)
	osgdebug_add_executable("${example}")
endforeach()
