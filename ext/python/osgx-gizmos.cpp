#include "osgx-python.hpp"
#include "osgx/Gizmos.hpp"
#include "osgx/PBR.hpp"

namespace osgx_python {

void bind_gizmos(py::module_& m_gizmo) {
	py::class_<
		osgx::gizmo::LightMarkers,
		osg::Group,
		osg::ref_ptr<osgx::gizmo::LightMarkers>
	>(m_gizmo, "LightMarkers")
		.def(
			py::init<const osgx::pbr::LightSet&, float, float>(),
			"lights"_a,
			"minMarkerRadius"_a=0.05f,
			"spotConeLength"_a=1.0f
		)
	;

	py::class_<osgx::gizmo::LightGizmos>(m_gizmo, "LightGizmos")
		.def(py::init<>())
		.def_readwrite("markers", &osgx::gizmo::LightGizmos::markers)
		.def_readwrite("overlay", &osgx::gizmo::LightGizmos::overlay)
	;

	m_gizmo.def(
		"createDirectionalOverlay",
		&osgx::gizmo::createDirectionalOverlay,
		"lights"_a,
		"scene"_a,
		"Non-depth-tested POST_RENDER wireframe plane+arrow overlay for LightSet's directional lights."
	);

	m_gizmo.def(
		"createLightGizmos",
		&osgx::gizmo::createLightGizmos,
		"lights"_a,
		"scene"_a,
		"minMarkerRadius"_a=0.05f,
		"spotConeLength"_a=1.0f,
		"Bundles LightMarkers (point/sphere/spot) and createDirectionalOverlay (directional) for a LightSet."
	);
}

}
