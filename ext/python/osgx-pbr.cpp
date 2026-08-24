#include "osgx-python.hpp"
#include "osgx/PBR.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Shader>
#include <osg/StateAttribute>
#include <osg/Texture2D>

OSGX_ENABLE_WARNINGS

namespace osgx_python {

void bind_pbr(py::module_& m_pbr) {
	osgx::registerPBRShaderLibs();

	m_pbr.attr("D_GGX") = osgx::D_GGX;
	m_pbr.attr("G_SCHLICK") = osgx::G_SCHLICK;
	m_pbr.attr("G_SMITH") = osgx::G_SMITH;
	m_pbr.attr("F_SCHLICK") = osgx::F_SCHLICK;
	m_pbr.attr("F_SCHLICK_ROUGHNESS") = osgx::F_SCHLICK_ROUGHNESS;
	m_pbr.attr("DIRECT_SPECULAR") = osgx::DIRECT_SPECULAR;
	m_pbr.attr("DIRECT_DIFFUSE") = osgx::DIRECT_DIFFUSE;
	m_pbr.attr("POINT_LIGHT_RADIANCE") = osgx::POINT_LIGHT_RADIANCE;
	m_pbr.attr("LIGHT_UNIFORMS") = osgx::LIGHT_UNIFORMS;
	m_pbr.attr("DIRECT_LIGHT") = osgx::DIRECT_LIGHT;
	m_pbr.attr("DIRECTIONAL_LIGHT_RADIANCE") = osgx::DIRECTIONAL_LIGHT_RADIANCE;
	m_pbr.attr("SPOT_LIGHT_RADIANCE") = osgx::SPOT_LIGHT_RADIANCE;
	m_pbr.attr("SPHERE_LIGHT_SPECULAR") = osgx::SPHERE_LIGHT_SPECULAR;
	m_pbr.attr("DIRECT_LIGHT_SPHERE") = osgx::DIRECT_LIGHT_SPHERE;
	m_pbr.attr("MAX_LIGHTS") = osgx::MAX_LIGHTS;
	m_pbr.attr("LIGHT_STRUCT_FLOATS") = osgx::LIGHT_STRUCT_FLOATS;
	m_pbr.attr("DIRECT_LIGHTING_DECL") = osgx::DIRECT_LIGHTING_DECL;
	m_pbr.attr("DIRECT_LIGHTING_HOOK_DEFAULT") = osgx::DIRECT_LIGHTING_HOOK_DEFAULT;
	m_pbr.attr("F_MULTISCATTER") = osgx::F_MULTISCATTER;
	m_pbr.attr("IBL_SPECULAR") = osgx::IBL_SPECULAR;
	m_pbr.attr("AMBIENT_LIGHTING_DECL") = osgx::AMBIENT_LIGHTING_DECL;
	m_pbr.attr("AMBIENT_LIGHTING_HOOK_DEFAULT") = osgx::AMBIENT_LIGHTING_HOOK_DEFAULT;
	m_pbr.attr("TONEMAP_PBR_NEUTRAL") = osgx::TONEMAP_PBR_NEUTRAL;
	m_pbr.attr("TONEMAP_DECL") = osgx::TONEMAP_DECL;
	m_pbr.attr("TONEMAP_HOOK_DEFAULT") = osgx::TONEMAP_HOOK_DEFAULT;
	m_pbr.attr("MATERIAL_BINDING") = osgx::MATERIAL_BINDING;

	// osgx::MaterialFactors/attachMaterialFactors() are gone -- collapsed into osgx::Material, a
	// real osg::StateAttribute (PBR.hpp/PBR.cpp). Bound the same way osgx-callbacks.cpp already
	// binds osgx::NodeCallbacksGroup et al. against a real pyosg-registered OSG base
	// (osg::StateAttribute is bound in pyosg/osg/State.cpp) -- cross-module inheritance works here
	// because both modules link the identical vendored pybind11 (same ABI tag), so pybind11's
	// process-wide type registry (populated at import time, not link time) already has
	// osg::StateAttribute by the time this module's own init runs. Requires `import osg` (pyosg)
	// to have already happened in this interpreter -- pybind11 has no way to resolve a base class
	// it hasn't seen registered yet.
	py::class_<osgx::Material, osg::StateAttribute, osg::ref_ptr<osgx::Material>>(m_pbr, "Material")
		.def(py::init<>())
		.def_property(
			"baseColor", &osgx::Material::getBaseColor, &osgx::Material::setBaseColor,
			"RGBA base color factor, multiplied against baseColorMap when one is set."
		)
		.def_property(
			"roughness", &osgx::Material::getRoughness, &osgx::Material::setRoughness
		)
		.def_property(
			"metallic", &osgx::Material::getMetallic, &osgx::Material::setMetallic
		)
		.def_property(
			"hasOcclusion", &osgx::Material::getHasOcclusion, &osgx::Material::setHasOcclusion,
			"Whether metallicRoughnessMap's R channel carries real per-pixel occlusion -- unlike "
			"the other has*Map flags, this has no dedicated texture of its own to derive from."
		)
		.def_property(
			"baseColorMap", &osgx::Material::getBaseColorMap, &osgx::Material::setBaseColorMap
		)
		.def_property(
			"normalMap", &osgx::Material::getNormalMap, &osgx::Material::setNormalMap
		)
		.def_property(
			"metallicRoughnessMap",
			&osgx::Material::getMetallicRoughnessMap, &osgx::Material::setMetallicRoughnessMap,
			"glTF's combined occlusion/roughness/metallic texture."
		)
		.def_property(
			"emissiveMap", &osgx::Material::getEmissiveMap, &osgx::Material::setEmissiveMap
		)
	;

	m_pbr.def("snippets", &osgx::snippets);

	// Assembles the osgx_DirectLighting() CONTRACT's default definition as a standalone FRAGMENT
	// osg::Shader, ready to add()/append() onto an osg::Program alongside a consumer's own
	// fragment shader (which only needs DIRECT_LIGHTING_DECL spliced in via #pragma osgx::pbr, plus a
	// call site) -- so a Python caller doesn't have to hand-assemble
	// osg.Shader(osg.Shader.FRAGMENT, osgx.resolveShaderLibs(osgx.pbr.DIRECT_LIGHTING_HOOK_DEFAULT))
	// itself. See PBR.hpp's DIRECT_LIGHTING_DECL/DIRECT_LIGHTING_HOOK_DEFAULT comment for the full
	// rationale.
	m_pbr.def(
		"makeDirectLightingHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::DIRECT_LIGHTING_HOOK_DEFAULT))
			));
		},
		"Builds the osgx_DirectLighting() CONTRACT's default-definition FRAGMENT shader object -- "
		"add it to a Program alongside a consumer fragment shader that only declares "
		"DIRECT_LIGHTING_DECL plus a call site."
	);

	// osgx_AmbientLighting() CONTRACT's Python-side convenience -- same shape as
	// makeDirectLightingHookShader() above. See PBR.hpp's AMBIENT_LIGHTING_DECL/
	// AMBIENT_LIGHTING_HOOK_DEFAULT comment: specular-only default (no SH-9 diffuse yet).
	m_pbr.def(
		"makeAmbientLightingHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::AMBIENT_LIGHTING_HOOK_DEFAULT))
			));
		},
		"Builds the osgx_AmbientLighting() CONTRACT's default-definition FRAGMENT shader object "
		"(specular-only IBL, via osgx_IBLSpecular) -- add it to a Program alongside a consumer "
		"fragment shader that only declares AMBIENT_LIGHTING_DECL plus a call site."
	);

	// osgx_Tonemap() CONTRACT's Python-side convenience -- same shape as
	// makeDirectLightingHookShader() above. See PBR.hpp's TONEMAP_DECL/TONEMAP_HOOK_DEFAULT comment.
	m_pbr.def(
		"makeTonemapHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::TONEMAP_HOOK_DEFAULT))
			));
		},
		"Builds the osgx_Tonemap() CONTRACT's default-definition FRAGMENT shader object "
		"(osgx_TonemapPBRNeutral) -- add it to a Program alongside a consumer fragment shader "
		"that only declares TONEMAP_DECL plus a call site."
	);

	py::class_<osgx::OrbitLightRig::Orbit>(m_pbr, "Orbit")
		.def(
			py::init([](float radius, float height, float speed, float phase, float intensity) {
				return osgx::OrbitLightRig::Orbit{radius, height, speed, phase, intensity};
			}),
			"radius"_a=0.5f,
			"height"_a=0.5f,
			"speed"_a=0.5f,
			"phase"_a=0.0f,
			"intensity"_a=1.0f
		)
		.def_readwrite("radius", &osgx::OrbitLightRig::Orbit::radius)
		.def_readwrite("height", &osgx::OrbitLightRig::Orbit::height)
		.def_readwrite("speed", &osgx::OrbitLightRig::Orbit::speed)
		.def_readwrite("phase", &osgx::OrbitLightRig::Orbit::phase)
		.def_readwrite("intensity", &osgx::OrbitLightRig::Orbit::intensity)
	;

	py::enum_<osgx::LightType>(m_pbr, "LightType")
		.value("Point", osgx::LightType::Point)
		.value("Directional", osgx::LightType::Directional)
		.value("Spot", osgx::LightType::Spot)
	;

	py::class_<osgx::LightSet>(m_pbr, "LightSet")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::LightSet::ss)
		.def_static("create", &osgx::LightSet::create)
		.def("valid", &osgx::LightSet::valid)
		.def(
			"setPoint",
			&osgx::LightSet::setPoint,
			"index"_a,
			"position"_a,
			"color"_a,
			"intensity"_a,
			"sourceRadius"_a=0.0f
		)
		.def("setDirectional", &osgx::LightSet::setDirectional, "index"_a, "direction"_a, "color"_a, "intensity"_a)
		.def(
			"setSpot",
			&osgx::LightSet::setSpot,
			"index"_a,
			"position"_a,
			"direction"_a,
			"color"_a,
			"intensity"_a,
			"innerConeAngle"_a,
			"outerConeAngle"_a,
			"sourceRadius"_a=0.0f
		)
		.def("setCount", &osgx::LightSet::setCount)
		.def("setPosition", &osgx::LightSet::setPosition, "index"_a, "position"_a, "intensity"_a)
		.def("getCount", &osgx::LightSet::getCount)
		.def("getPosIntensity", &osgx::LightSet::getPosIntensity, "index"_a)
		.def("getColor", &osgx::LightSet::getColor, "index"_a)
		.def("getType", &osgx::LightSet::getType, "index"_a)
		.def("getDirection", &osgx::LightSet::getDirection, "index"_a)
		.def("getSpotAngles", &osgx::LightSet::getSpotAngles, "index"_a)
		.def("getSourceRadius", &osgx::LightSet::getSourceRadius, "index"_a)
	;

	py::class_<
		osgx::OrbitLightRig,
		osg::NodeCallback,
		osg::ref_ptr<osgx::OrbitLightRig>
	>(m_pbr, "OrbitLightRig")
		.def(py::init<>())
		.def_readwrite("lights", &osgx::OrbitLightRig::lights)
		.def_readwrite("center", &osgx::OrbitLightRig::center)
		.def_readwrite("intensity", &osgx::OrbitLightRig::intensity)
		.def_readwrite("orbits", &osgx::OrbitLightRig::orbits)
	;
}

}
