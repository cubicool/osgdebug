#include "osgx-python.hpp"
#include "osgx/Shadow.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Texture2D>
#include <osg/Uniform>

OSGX_ENABLE_WARNINGS

namespace osgx_python {

void bind_shadow(py::module_& m_shadow) {
	osgx::shadow::registerShaderLibs();

	m_shadow.attr("SHADOW_UNIFORMS") = osgx::shadow::SHADOW_UNIFORMS;
	m_shadow.attr("SHADOW_FACTOR") = osgx::shadow::SHADOW_FACTOR;
	m_shadow.attr("DIRECT_LIGHTING_HOOK_SHADOWED") = osgx::shadow::DIRECT_LIGHTING_HOOK_SHADOWED;

	// Python-side convenience mirroring osgx.pbr.makeDirectLightingHookShader() -- assembles
	// DIRECT_LIGHTING_HOOK_SHADOWED as a standalone FRAGMENT osg::Shader, ready to add()/append()
	// onto an osg::Program in place of osgx.pbr.makeDirectLightingHookShader()'s unshadowed one.
	m_shadow.def(
		"makeShadowedDirectLightingHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::shadow::DIRECT_LIGHTING_HOOK_SHADOWED))
			));
		},
		"Builds the osgx_DirectLighting() CONTRACT's shadowed-definition FRAGMENT shader object -- "
		"same contract as osgx.pbr.makeDirectLightingHookShader(), but the light at "
		"osgx_shadowCasterIndex is multiplied by osgx_ShadowFactor()."
	);

	py::class_<osgx::shadow::ShadowMapOptions>(m_shadow, "ShadowMapOptions")
		.def(py::init<>())
		.def_readwrite("size", &osgx::shadow::ShadowMapOptions::size)
		.def_readwrite("halfFovDegrees", &osgx::shadow::ShadowMapOptions::halfFovDegrees)
		.def_readwrite("margin", &osgx::shadow::ShadowMapOptions::margin)
		.def_readwrite("bias", &osgx::shadow::ShadowMapOptions::bias)
		.def_readwrite("strength", &osgx::shadow::ShadowMapOptions::strength)
	;

	py::class_<osgx::shadow::ShadowMap>(m_shadow, "ShadowMap")
		.def(py::init<>())
		.def_readwrite("camera", &osgx::shadow::ShadowMap::camera)
		.def_readwrite("depthTexture", &osgx::shadow::ShadowMap::depthTexture)
		.def_readwrite("shadowMatrix", &osgx::shadow::ShadowMap::shadowMatrix)
		.def_readwrite("bias", &osgx::shadow::ShadowMap::bias)
		.def_readwrite("strength", &osgx::shadow::ShadowMap::strength)
		.def_readwrite("casterIndex", &osgx::shadow::ShadowMap::casterIndex)
		.def_readwrite("lightView", &osgx::shadow::ShadowMap::lightView)
		.def_readwrite("lightProj", &osgx::shadow::ShadowMap::lightProj)
		.def("valid", &osgx::shadow::ShadowMap::valid)
	;

	m_shadow.def(
		"createDirectionalShadowMap",
		&osgx::shadow::createDirectionalShadowMap,
		"lightDirection"_a,
		"sceneBoundCenter"_a,
		"sceneBoundRadius"_a,
		"options"_a=osgx::shadow::ShadowMapOptions{},
		"Builds a directional shadow map (PRE_RENDER depth camera + shadow-matrix uniform) sized "
		"and placed to keep near:far depth precision sane regardless of scene scale. `camera` "
		"still needs adding to the scene graph by the caller."
	);
	m_shadow.def(
		"updateShadowMatrix",
		&osgx::shadow::updateShadowMatrix,
		"shadowMap"_a,
		"Recomputes shadowMatrix from shadowMap.lightView/lightProj -- only needed after mutating "
		"either directly (e.g. a moving light); a no-op to call redundantly otherwise."
	);
}

}
