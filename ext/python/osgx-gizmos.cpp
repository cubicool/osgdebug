#include "osgx-python.hpp"
#include "osgx/Gizmos.hpp"
#include "osgx/PBR.hpp"

namespace osgx_python {

void bind_gizmos(py::module_& m) {
	py::class_<
		osgx::LightMarkers,
		osg::Group,
		osg::ref_ptr<osgx::LightMarkers>
	>(m, "LightMarkers")
		.def(
			py::init<const osgx::LightSet&, float, float>(),
			"lights"_a,
			"minMarkerRadius"_a=0.05f,
			"spotConeLength"_a=1.0f
		)
	;

	py::class_<
		osgx::LightGizmos,
		osg::Group,
		osg::ref_ptr<osgx::LightGizmos>
	>(m, "LightGizmos")
		.def(
			py::init<const osgx::LightSet&, osg::Node*, float, float>(),
			"lights"_a,
			"scene"_a,
			"minMarkerRadius"_a=0.05f,
			"spotConeLength"_a=1.0f,
			"Bundles LightMarkers (point/sphere/spot) and a directional-only overlay camera for a "
			"LightSet into one addable node."
		)
		.def_property_readonly("markers", &osgx::LightGizmos::getMarkers)
		.def_property_readonly("overlay", &osgx::LightGizmos::getOverlay)
	;
}

}
