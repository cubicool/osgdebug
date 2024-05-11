include(ExternalProject)

# set_directory_properties(PROPERTIES EP_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../external_libs")

ExternalProject_Add(
	OSG
	GIT_REPOSITORY https://github.com/openscenegraph/OpenSceneGraph.git
	# GIT_TAG 6d20aaba44af2b039ebfb70f354864e406ebb2dd
	# GIT_TAG 0123456789
	GIT_TAG OpenSceneGraph-3.6.5
	GIT_SHALLOW ON
	# UPDATE_COMMAND ""
	# CONFIGURE_COMMAND ""
	# BUILD_COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> make ARCH=64 ARCHINST=64 -C<SOURCE_DIR> -j${CMAKE_BUILD_PARALLEL_LEVEL}
	# BUILD_COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> make ARCH=64 ARCHINST=64 -C<SOURCE_DIR>/OpenThreads -j${CMAKE_BUILD_PARALLEL_LEVEL}
	# COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> make ARCH=64 ARCHINST=64 -C<SOURCE_DIR>/OpenThreads install
	# COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> OPENTHREADS_LIB_DIR=<INSTALL_DIR>/lib OPENTHREADS_INC_DIR=<INSTALL_DIR>/include make ARCH=64 ARCHINST=64 -C<SOURCE_DIR>/Producer -j${CMAKE_BUILD_PARALLEL_LEVEL}
	# COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> OPENTHREADS_LIB_DIR=<INSTALL_DIR>/lib OPENTHREADS_INC_DIR=<INSTALL_DIR>/include make ARCH=64 ARCHINST=64 -C<SOURCE_DIR>/Producer install
	# COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> OPENTHREADS_LIB_DIR=<INSTALL_DIR>/lib OPENTHREADS_INC_DIR=<INSTALL_DIR>/include OPENPRODUCER_LIB_DIR=<INSTALL_DIR>/lib OPENPRODUCER_INC_DIR=<INSTALL_DIR>/include make ARCH=64 ARCHINST=64 COMPILE_EXAMPLES=yes -C<SOURCE_DIR>/OpenSceneGraph -j${CMAKE_BUILD_PARALLEL_LEVEL}
	# COMMAND ${CMAKE_COMMAND} -E env INST_LOCATION=<INSTALL_DIR> OPENTHREADS_LIB_DIR=<INSTALL_DIR>/lib OPENTHREADS_INC_DIR=<INSTALL_DIR>/include OPENPRODUCER_LIB_DIR=<INSTALL_DIR>/lib OPENPRODUCER_INC_DIR=<INSTALL_DIR>/include make ARCH=64 ARCHINST=64 -C<SOURCE_DIR>/OpenSceneGraph install
	# INSTALL_COMMAND ""
	# PREFIX ${CMAKE_CURRENT_BINARY_DIR}/OSG
	PREFIX ${CMAKE_CURRENT_BINARY_DIR}
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DOSG_AGGRESSIVE_WARNING_FLAGS="-Wno-deprecated-copy" -DOSG_AGGRESSIVE_WARNINGS=ON
)

ExternalProject_Get_Property(OSG INSTALL_DIR)

# set(OPENTHREADS_LIB_DIR ${INSTALL_DIR}/lib)
# file(MAKE_DIRECTORY ${OPENTHREADS_LIB_DIR})
# set(OPENTHREADS_INC_DIR ${INSTALL_DIR}/include)
# file(MAKE_DIRECTORY ${OPENTHREADS_INC_DIR})
#
# set(PRODUCER_LIB_DIR ${INSTALL_DIR}/lib)
# file(MAKE_DIRECTORY ${PRODUCER_LIB_DIR})
# set(PRODUCER_INC_DIR ${INSTALL_DIR}/include)
# file(MAKE_DIRECTORY ${PRODUCER_INC_DIR})
#
# set(OPENSCENEGRAPH_INC_DIR ${INSTALL_DIR}/include)
# file(MAKE_DIRECTORY ${OPENSCENEGRAPH_INC_DIR})
# set(OPENSCENEGRAPH_LIB_DIR ${INSTALL_DIR}/lib64)
# file(MAKE_DIRECTORY ${OPENSCENEGRAPH_LIB_DIR})

add_library(osgDebug_OSG INTERFACE)
add_dependencies(osgDebug_OSG OSG OpenThreads)
target_include_directories(osgDebug_OSG INTERFACE
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)
target_link_directories(osgDebug_OSG INTERFACE
	"${CMAKE_CURRENT_BINARY_DIR}/lib"
)
target_link_libraries(osgDebug_OSG INTERFACE
	OpenThreads
	osg
	osgDB
	osgViewer
	osgGA
	osgUtil
	osgSim
	osgText
	"-Wl,--disable-new-dtags"
)
# target_compile_options(osgDebug_OSG INTERFACE -Wno-deprecated-copy)

# XXX: This was (potentially) deprecated with the move to OSG 3.6.5/Ubuntu 20.04
# ExternalProject_Add(
#     external_geographiclib
#     # PREFIX geographiclib
#     GIT_REPOSITORY  https://github.com/geographiclib/geographiclib.git
#     GIT_TAG r1.52
#     GIT_SHALLOW ON
#     UPDATE_COMMAND ""
#     CMAKE_ARGS
#         -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
#         -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#         -DGEOGRAPHICLIB_DATA=<INSTALL_DIR>
# )
#
# ExternalProject_Get_Property(external_geographiclib INSTALL_DIR)
# set(GEOGRAPHICLIB_INCLUDE_DIRS ${INSTALL_DIR}/include)
# set(GEOGRAPHICLIB_LIB_DIR ${INSTALL_DIR}/lib)
# file(MAKE_DIRECTORY  ${GEOGRAPHICLIB_INCLUDE_DIRS})
# file(MAKE_DIRECTORY  ${GEOGRAPHICLIB_LIB_DIR})
#
# add_library(geographiclib_ext_dep INTERFACE)
#
# add_dependencies(geographiclib_ext_dep
# external_geographiclib)
#
# target_include_directories(geographiclib_ext_dep INTERFACE
# ${GEOGRAPHICLIB_INCLUDE_DIRS})
#
# target_link_directories(geographiclib_ext_dep INTERFACE
# ${GEOGRAPHICLIB_LIB_DIR})
#
# target_link_libraries(geographiclib_ext_dep INTERFACE
# Geographic)
