add_library("osgdb_debug" SHARED
	"ReaderWriterDEBUG.cpp"
	"../osgx.hpp"
	"../osgDebug.hpp"
)

osgdebug_compile_options("osgdb_debug")
