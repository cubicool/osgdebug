#include "osgx-python.hpp"
#include "osgx/PBR.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Shader>

OSGX_ENABLE_WARNINGS

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
	m_pbr.attr("LIGHT_STRUCT_FLOATS") = osgx::pbr::LIGHT_STRUCT_FLOATS;
	m_pbr.attr("LIGHT_SSBO_BINDING") = osgx::pbr::LIGHT_SSBO_BINDING;
	m_pbr.attr("LIGHT_SHADE_DECL") = osgx::pbr::LIGHT_SHADE_DECL;
	m_pbr.attr("DIRECT_LIGHT_HOOK_DEFAULT") = osgx::pbr::DIRECT_LIGHT_HOOK_DEFAULT;
	m_pbr.attr("F_MULTISCATTER") = osgx::pbr::F_MULTISCATTER;
	m_pbr.attr("IBL_SPECULAR") = osgx::pbr::IBL_SPECULAR;
	m_pbr.attr("TONEMAP_PBR_NEUTRAL") = osgx::pbr::TONEMAP_PBR_NEUTRAL;

	m_pbr.def("snippets", &osgx::pbr::snippets);

	// Assembles the osgx_ShadeDirect() CONTRACT's default definition as a standalone FRAGMENT
	// osg::Shader, ready to add()/append() onto an osg::Program alongside a consumer's own
	// fragment shader (which only needs LIGHT_SHADE_DECL spliced in via #pragma osgx::pbr, plus a
	// call site) -- so a Python caller doesn't have to hand-assemble
	// osg.Shader(osg.Shader.FRAGMENT, osgx.resolveShaderLibs(osgx.pbr.DIRECT_LIGHT_HOOK_DEFAULT))
	// itself. See PBR.hpp's LIGHT_SHADE_DECL/DIRECT_LIGHT_HOOK_DEFAULT comment for the full
	// rationale.
	m_pbr.def(
		"makeDirectLightHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::pbr::DIRECT_LIGHT_HOOK_DEFAULT))
			));
		},
		"Builds the osgx_ShadeDirect() CONTRACT's default-definition FRAGMENT shader object -- "
		"add it to a Program alongside a consumer fragment shader that only declares "
		"LIGHT_SHADE_DECL plus a call site."
	);

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

	py::enum_<osgx::pbr::LightType>(m_pbr, "LightType")
		.value("Point", osgx::pbr::LightType::Point)
		.value("Directional", osgx::pbr::LightType::Directional)
		.value("Spot", osgx::pbr::LightType::Spot)
	;

	py::class_<osgx::pbr::LightSet>(m_pbr, "LightSet")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::pbr::LightSet::ss)
		.def_static("create", &osgx::pbr::LightSet::create)
		.def("valid", &osgx::pbr::LightSet::valid)
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
		.def("setPosition", &osgx::pbr::LightSet::setPosition, "index"_a, "position"_a, "intensity"_a)
		.def("getCount", &osgx::pbr::LightSet::getCount)
		.def("getPosIntensity", &osgx::pbr::LightSet::getPosIntensity, "index"_a)
		.def("getColor", &osgx::pbr::LightSet::getColor, "index"_a)
		.def("getType", &osgx::pbr::LightSet::getType, "index"_a)
		.def("getDirection", &osgx::pbr::LightSet::getDirection, "index"_a)
		.def("getSpotAngles", &osgx::pbr::LightSet::getSpotAngles, "index"_a)
		.def("getSourceRadius", &osgx::pbr::LightSet::getSourceRadius, "index"_a)
	;

	py::class_<
		osgx::pbr::OrbitLightRig,
		osg::NodeCallback,
		osg::ref_ptr<osgx::pbr::OrbitLightRig>
	>(m_pbr, "OrbitLightRig")
		.def(py::init<>())
		.def_readwrite("lights", &osgx::pbr::OrbitLightRig::lights)
		.def_readwrite("center", &osgx::pbr::OrbitLightRig::center)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::intensity)
		.def_readwrite("orbits", &osgx::pbr::OrbitLightRig::orbits)
	;
}

}
