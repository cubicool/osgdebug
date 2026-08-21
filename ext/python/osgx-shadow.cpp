#include "osgx-python.hpp"
#include "osgx/Shadow.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Texture2D>
#include <osg/Uniform>

OSGX_ENABLE_WARNINGS

namespace osgx_python {

void bind_shadow(py::module_& m_shadow) {
	osgx::registerShadowShaderLibs();

	m_shadow.attr("SHADOW_UNIFORMS") = osgx::SHADOW_UNIFORMS;
	m_shadow.attr("SHADOW_FACTOR") = osgx::SHADOW_FACTOR;
	m_shadow.attr("DIRECT_LIGHTING_HOOK_SHADOWED") = osgx::DIRECT_LIGHTING_HOOK_SHADOWED;

	// Python-side convenience mirroring osgx.pbr.makeDirectLightingHookShader() -- assembles
	// DIRECT_LIGHTING_HOOK_SHADOWED as a standalone FRAGMENT osg::Shader, ready to add()/append()
	// onto an osg::Program in place of osgx.pbr.makeDirectLightingHookShader()'s unshadowed one.
	m_shadow.def(
		"makeShadowedDirectLightingHookShader",
		[]() {
			return osg::ref_ptr<osg::Shader>(new osg::Shader(
				osg::Shader::FRAGMENT,
				osgx::resolveShaderLibs(std::string(osgx::DIRECT_LIGHTING_HOOK_SHADOWED))
			));
		},
		"Builds the osgx_DirectLighting() CONTRACT's shadowed-definition FRAGMENT shader object -- "
		"same contract as osgx.pbr.makeDirectLightingHookShader(), but the light at "
		"osgx_shadowCasterIndex is multiplied by osgx_ShadowFactor()."
	);

	py::class_<osgx::ShadowMapOptions>(m_shadow, "ShadowMapOptions")
		.def(py::init<>())
		.def_readwrite("size", &osgx::ShadowMapOptions::size)
		.def_readwrite("extent", &osgx::ShadowMapOptions::extent)
		.def_readwrite("margin", &osgx::ShadowMapOptions::margin)
		.def_readwrite("bias", &osgx::ShadowMapOptions::bias)
		.def_readwrite("strength", &osgx::ShadowMapOptions::strength)
	;

	py::class_<osgx::ShadowMap>(m_shadow, "ShadowMap")
		.def(py::init<>())
		.def_readwrite("camera", &osgx::ShadowMap::camera)
		.def_readwrite("depthTexture", &osgx::ShadowMap::depthTexture)
		.def_readwrite("shadowMatrix", &osgx::ShadowMap::shadowMatrix)
		.def_readwrite("bias", &osgx::ShadowMap::bias)
		.def_readwrite("strength", &osgx::ShadowMap::strength)
		.def_readwrite("casterIndex", &osgx::ShadowMap::casterIndex)
		.def_readwrite("lightView", &osgx::ShadowMap::lightView)
		.def_readwrite("lightProj", &osgx::ShadowMap::lightProj)
		.def("valid", &osgx::ShadowMap::valid)
		.def_static(
			"create",
			&osgx::ShadowMap::create,
			"lightDirection"_a,
			"sceneBoundCenter"_a,
			"sceneBoundRadius"_a,
			"options"_a=osgx::ShadowMapOptions{},
			"Builds a directional shadow map (PRE_RENDER depth camera + shadow-matrix uniform) sized "
			"and placed to keep near:far depth precision sane regardless of scene scale. `camera` "
			"still needs adding to the scene graph by the caller."
		)
		.def(
			"updateMatrix",
			&osgx::ShadowMap::updateMatrix,
			"Recomputes shadowMatrix from lightView/lightProj -- only needed after mutating "
			"either directly; a no-op to call redundantly otherwise. Does NOT reposition the camera "
			"itself -- see reposition() for that."
		)
		.def(
			"reposition",
			&osgx::ShadowMap::reposition,
			"lightDirection"_a,
			"sceneBoundCenter"_a,
			"sceneBoundRadius"_a,
			"options"_a=osgx::ShadowMapOptions{},
			"Repositions this EXISTING ShadowMap for a new light direction/scene bound, in place -- "
			"no new camera/FBO/depth-texture allocation, just recomputed view/projection matrices. "
			"Cheap enough to call every frame (or on every GUI-slider tick) for an interactively-moving "
			"light; create() remains correct for a light fixed at scene-build time."
		)
	;
}

}
