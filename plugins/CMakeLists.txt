add_library("osgdb_debug" SHARED
	"ReaderWriterDEBUG.cpp"
	"../osgx/osgx.hpp"
)

osgx_compile_options("osgdb_debug")
target_link_libraries("osgdb_debug" PRIVATE osgx::osgx)
