#include "osgx/Shadow.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GL>
#include <osg/Math>
#include <osg/Matrix>
#include <osg/Matrixd>
#include <osg/Matrixf>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <cmath>

namespace osgx::shadow {

bool ShadowMap::valid() const {
	return camera.valid() && depthTexture.valid() && shadowMatrix.valid();
}

ShadowMap createDirectionalShadowMap(
	const osg::Vec3& lightDirection,
	const osg::Vec3& sceneBoundCenter,
	float sceneBoundRadius,
	const ShadowMapOptions& options
) {
	ShadowMap result;

	osg::Vec3 dir = lightDirection;

	dir.normalize();

	const double halfFovRad = osg::DegreesToRadians(double(options.halfFovDegrees));
	const double distance = double(sceneBoundRadius) * double(options.margin) / std::tan(halfFovRad);
	const osg::Vec3 lightPos = sceneBoundCenter - dir * float(distance);

	// Up vector (0,1,0), not (0,0,1) -- a light direction nearly aligned with world-up produces a
	// degenerate lookAt with (0,0,1) (same failure mode 08-shadows.py/09-ibl.py both noted); (0,1,0)
	// sidesteps it for every direction any pyosg-lighting example has used.
	result.lightView = osg::Matrix::lookAt(lightPos, sceneBoundCenter, osg::Vec3(0.0, 1.0, 0.0));

	const double margin = double(sceneBoundRadius) * double(options.margin);
	const double near_ = std::max(0.01, distance - margin);
	const double far_ = distance + margin;

	result.lightProj = osg::Matrix::perspective(2.0 * double(options.halfFovDegrees), 1.0, near_, far_);

	result.depthTexture = osgx::make_ref<osg::Texture2D>();
	result.depthTexture->setTextureSize(options.size, options.size);
	result.depthTexture->setInternalFormat(GL_DEPTH_COMPONENT24);
	result.depthTexture->setSourceFormat(GL_DEPTH_COMPONENT);
	result.depthTexture->setSourceType(GL_FLOAT);
	result.depthTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	result.depthTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	result.depthTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	result.depthTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	result.camera = osgx::make_ref<osg::Camera>();
	result.camera->setName("osgx_shadow_DirectionalShadowMap");
	result.camera->setRenderOrder(osg::Camera::PRE_RENDER);
	result.camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	result.camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	result.camera->setClearMask(GL_DEPTH_BUFFER_BIT);
	result.camera->setClearDepth(1.0);
	result.camera->setViewport(0, 0, options.size, options.size);
	result.camera->attach(osg::Camera::DEPTH_BUFFER, result.depthTexture);
	// Depth-only: the old hand-rolled Python examples attached a dummy color texture here to work
	// around a since-irrelevant pybind11 binding gap (Camera::setDrawBuffer/setReadBuffer weren't
	// exposed to Python yet) -- ordinary C++ calls, no workaround needed.
	result.camera->setDrawBuffer(GL_NONE);
	result.camera->setReadBuffer(GL_NONE);
	result.camera->setViewMatrix(result.lightView);
	result.camera->setProjectionMatrix(result.lightProj);

	result.shadowMatrix = new osg::Uniform("osgx_shadowMatrix", osg::Matrixf::identity());
	result.bias = new osg::Uniform("osgx_shadowBias", options.bias);
	result.strength = new osg::Uniform("osgx_shadowStrength", options.strength);
	result.casterIndex = new osg::Uniform("osgx_shadowCasterIndex", 0);

	updateShadowMatrix(result);

	return result;
}

void updateShadowMatrix(ShadowMap& shadowMap) {
	if(!shadowMap.shadowMatrix) return;

	// OSG row-vector convention: worldPos * (lightView * lightProj) is the same composition GLSL's
	// osgx_shadowMatrix * vec4(worldPos, 1.0) performs once uploaded -- see Shadow.hpp's file-level
	// comment for why this needs no main-camera term (unlike the eye-space hand-rolled examples).
	shadowMap.shadowMatrix->set(osg::Matrixf(shadowMap.lightView * shadowMap.lightProj));
}

void registerShaderLibs() {
	static const osgx::ShaderLib libs[] = {
		{"SHADOW_UNIFORMS", "osgx_shadowMap", SHADOW_UNIFORMS},
		{"SHADOW_FACTOR", "osgx_ShadowFactor", SHADOW_FACTOR},
	};

	::osgx::registerShaderLibs("osgx::shadow", libs);
}

}
