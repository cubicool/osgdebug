#include "osgx/GBuffer.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GL>

OSGX_ENABLE_WARNINGS

namespace osgx {

namespace {

GLint internalFormatFor(AttachmentFormat format) {
	switch(format) {
		case AttachmentFormat::RGBA8: return GL_RGBA;
		case AttachmentFormat::RGB16F: return GL_RGB16F;
		case AttachmentFormat::RGBA16F: return GL_RGBA16F;
		case AttachmentFormat::RGBA32F: return GL_RGBA32F;
	}

	return GL_RGBA;
}

}

bool GBuffer::valid() const {
	return camera.valid() && !colorTextures.empty() && depthTexture.valid();
}

GBuffer GBuffer::create(
	osg::Node* node,
	int width,
	int height,
	std::span<const AttachmentFormat> colorFormats,
	osg::Transform::ReferenceFrame referenceFrame
) {
	GBuffer result;

	if(!node || colorFormats.empty()) return result;

	for(const auto format: colorFormats) {
		auto tex = osgx::make_ref<osg::Texture2D>();

		tex->setTextureSize(width, height);
		tex->setInternalFormat(internalFormatFor(format));
		tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
		tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
		tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
		tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
		// Every one of these is BOTH a render target (written here, every frame) AND a sampler
		// input in a later pass (SSAO/lighting) -- without DYNAMIC, OSG's default StateAttribute
		// caching can treat a texture as unchanging after its first successful bind and stop
		// correctly re-applying it on later frames (same fix already required for
		// osgx-gbuffer.cpp's own hdr_color_tex/ao_tex-style RTT textures, and for every RTT
		// texture in pyosg-lighting's own examples -- see 11-sketchfab.py).
		tex->setDataVariance(osg::Object::DYNAMIC);

		result.colorTextures.push_back(tex);
	}

	result.depthTexture = osgx::make_ref<osg::Texture2D>();
	result.depthTexture->setTextureSize(width, height);
	result.depthTexture->setInternalFormat(GL_DEPTH_COMPONENT24);
	result.depthTexture->setSourceFormat(GL_DEPTH_COMPONENT);
	result.depthTexture->setSourceType(GL_FLOAT);
	result.depthTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	result.depthTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	result.depthTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	result.depthTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	result.depthTexture->setDataVariance(osg::Object::DYNAMIC);

	result.camera = osgx::make_ref<osg::Camera>();
	result.camera->setName("osgx_gbuffer_GeometryPass");
	result.camera->setRenderOrder(osg::Camera::PRE_RENDER);
	result.camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	result.camera->setReferenceFrame(referenceFrame);
	result.camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	result.camera->setClearColor(osg::Vec4(0.0, 0.0, 0.0, 0.0));
	result.camera->setViewport(0, 0, width, height);

	for(std::size_t i = 0; i < result.colorTextures.size(); i++) {
		result.camera->attach(
			static_cast<osg::Camera::BufferComponent>(osg::Camera::COLOR_BUFFER0 + i),
			result.colorTextures[i]
		);
	}

	result.camera->attach(osg::Camera::DEPTH_BUFFER, result.depthTexture);
	result.camera->addChild(node);

	return result;
}

}
