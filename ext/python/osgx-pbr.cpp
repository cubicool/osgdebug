#include "osgx-python.hpp"
#include "osgx/PBR.hpp"

namespace osgx_python {

void bind_pbr(py::module_& m_pbr) {
	osgx::pbr::registerShaderLibs();

	m_pbr.attr("D_GGX") = osgx::pbr::D_GGX;
	m_pbr.attr("G_SCHLICK") = osgx::pbr::G_SCHLICK;
	m_pbr.attr("G_SMITH") = osgx::pbr::G_SMITH;
	m_pbr.attr("F_SCHLICK") = osgx::pbr::F_SCHLICK;
	m_pbr.attr("F_SCHLICK_ROUGHNESS") = osgx::pbr::F_SCHLICK_ROUGHNESS;
	m_pbr.attr("DIRECT_SPECULAR") = osgx::pbr::DIRECT_SPECULAR;
	m_pbr.attr("DIRECT_DIFFUSE") = osgx::pbr::DIRECT_DIFFUSE;
	m_pbr.attr("POINT_LIGHT_RADIANCE") = osgx::pbr::POINT_LIGHT_RADIANCE;
	m_pbr.attr("LIGHT_UNIFORMS") = osgx::pbr::LIGHT_UNIFORMS;
	m_pbr.attr("DIRECT_LIGHT") = osgx::pbr::DIRECT_LIGHT;
	m_pbr.attr("DIRECTIONAL_LIGHT_RADIANCE") = osgx::pbr::DIRECTIONAL_LIGHT_RADIANCE;
	m_pbr.attr("SPOT_LIGHT_RADIANCE") = osgx::pbr::SPOT_LIGHT_RADIANCE;
	m_pbr.attr("SPHERE_LIGHT_SPECULAR") = osgx::pbr::SPHERE_LIGHT_SPECULAR;
	m_pbr.attr("DIRECT_LIGHT_SPHERE") = osgx::pbr::DIRECT_LIGHT_SPHERE;
	m_pbr.attr("MAX_LIGHTS") = osgx::pbr::MAX_LIGHTS;
	m_pbr.attr("F_MULTISCATTER") = osgx::pbr::F_MULTISCATTER;
	m_pbr.attr("IBL_SPECULAR") = osgx::pbr::IBL_SPECULAR;
	m_pbr.attr("TONEMAP_PBR_NEUTRAL") = osgx::pbr::TONEMAP_PBR_NEUTRAL;

	m_pbr.def("snippets", &osgx::pbr::snippets);

	py::class_<osgx::pbr::OrbitLightRig::Orbit>(m_pbr, "Orbit")
		.def(
			py::init([](float radius, float height, float speed, float phase, float intensity) {
				return osgx::pbr::OrbitLightRig::Orbit{radius, height, speed, phase, intensity};
			}),
			"radius"_a=0.5f,
			"height"_a=0.5f,
			"speed"_a=0.5f,
			"phase"_a=0.0f,
			"intensity"_a=1.0f
		)
		.def_readwrite("radius", &osgx::pbr::OrbitLightRig::Orbit::radius)
		.def_readwrite("height", &osgx::pbr::OrbitLightRig::Orbit::height)
		.def_readwrite("speed", &osgx::pbr::OrbitLightRig::Orbit::speed)
		.def_readwrite("phase", &osgx::pbr::OrbitLightRig::Orbit::phase)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::Orbit::intensity)
	;

	py::class_<
		osgx::pbr::OrbitLightRig,
		osg::NodeCallback,
		osg::ref_ptr<osgx::pbr::OrbitLightRig>
	>(m_pbr, "OrbitLightRig")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::pbr::OrbitLightRig::ss)
		.def_readwrite("center", &osgx::pbr::OrbitLightRig::center)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::intensity)
		.def_readwrite("uniformName", &osgx::pbr::OrbitLightRig::uniformName)
		.def_readwrite("orbits", &osgx::pbr::OrbitLightRig::orbits)
	;

	py::enum_<osgx::pbr::LightType>(m_pbr, "LightType")
		.value("Point", osgx::pbr::LightType::Point)
		.value("Directional", osgx::pbr::LightType::Directional)
		.value("Spot", osgx::pbr::LightType::Spot)
	;

	py::class_<osgx::pbr::LightSet>(m_pbr, "LightSet")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::pbr::LightSet::ss)
		.def_static("create", &osgx::pbr::LightSet::create)
		.def(
			"setPoint",
			&osgx::pbr::LightSet::setPoint,
			"index"_a,
			"position"_a,
			"color"_a,
			"intensity"_a,
			"sourceRadius"_a=0.0f
		)
		.def("setDirectional", &osgx::pbr::LightSet::setDirectional, "index"_a, "direction"_a, "color"_a, "intensity"_a)
		.def(
			"setSpot",
			&osgx::pbr::LightSet::setSpot,
			"index"_a,
			"position"_a,
			"direction"_a,
			"color"_a,
			"intensity"_a,
			"innerConeAngle"_a,
			"outerConeAngle"_a,
			"sourceRadius"_a=0.0f
		)
		.def("setCount", &osgx::pbr::LightSet::setCount)
	;
}

}
