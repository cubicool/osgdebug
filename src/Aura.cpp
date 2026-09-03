#include "osgx/Aura.hpp"
#include "osgx/IBL.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GL>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <array>
#include <span>

namespace osgx {

namespace {

constexpr const char SELECTION_VERTEX_SHADER[] = R"GLSL(
#version 430 core

in vec4 osg_Vertex;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;

out float vEyeDepth;

void main() {
	vEyeDepth = (osg_ModelViewMatrix * osg_Vertex).z;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)GLSL";

// MRT: COLOR_BUFFER0 = originalMask (unchanged contract), COLOR_BUFFER1 = originalDepth (new).
// One geometry pass either way -- adding a second render target here is what keeps the whole
// pipeline at 3 passes instead of a separate depth-capture pass.
constexpr const char SELECTION_FRAGMENT_SHADER[] = R"GLSL(
#version 430 core

in float vEyeDepth;

layout(location = 0) out vec4 fragMask;
layout(location = 1) out vec4 fragDepth;

void main() {
	fragMask = vec4(1.0);
	fragDepth = vec4(vEyeDepth, 0.0, 0.0, 1.0);
}
)GLSL";

// Propagates mask AND depth as VALUES through the nearest-neighbor search -- not a UV to re-sample
// later. dilatedX layout: r=found, g=depth, b=X-distance-so-far, a=unused.
constexpr const char DILATE_X_FRAGMENT_SHADER[] = R"GLSL(
#version 430 core

uniform sampler2D auraOriginalMask;
uniform sampler2D auraOriginalDepth;
uniform int auraRadius;

in vec2 vUV;
out vec4 fragColor;

void main() {
	vec2 texel = 1.0 / vec2(textureSize(auraOriginalMask, 0));
	vec4 result = vec4(0.0);

	for(int distance = 0; distance <= 64; distance++) {
		if(distance > auraRadius) break;

		vec2 sourceUV = vUV - vec2(float(distance), 0.0) * texel;

		if(texture(auraOriginalMask, sourceUV).r > 0.5) {
			result = vec4(1.0, texture(auraOriginalDepth, sourceUV).r, float(distance), 0.0);

			break;
		}

		sourceUV = vUV + vec2(float(distance), 0.0) * texel;

		if(distance != 0 && texture(auraOriginalMask, sourceUV).r > 0.5) {
			result = vec4(1.0, texture(auraOriginalDepth, sourceUV).r, float(distance), 0.0);

			break;
		}
	}

	fragColor = result;
}
)GLSL";

// expanded layout: r=found, g=depth (propagated through from dilatedX, itself propagated through
// from originalDepth), b=unused, a=Chebyshev distance in pixels.
constexpr const char DILATE_Y_FRAGMENT_SHADER[] = R"GLSL(
#version 430 core

uniform sampler2D auraDilatedX;
uniform int auraRadius;

in vec2 vUV;
out vec4 fragColor;

void main() {
	vec2 texel = 1.0 / vec2(textureSize(auraDilatedX, 0));
	vec4 result = vec4(0.0);
	float nearestDistance = float(auraRadius) + 1.0;

	for(int distance = 0; distance <= 64; distance++) {
		if(distance > auraRadius) break;

		vec4 candidate = texture(auraDilatedX, vUV - vec2(0.0, float(distance)) * texel);

		if(candidate.r > 0.5) {
			float candidateDistance = max(candidate.b, float(distance));

			if(candidateDistance < nearestDistance) {
				result = vec4(1.0, candidate.g, 0.0, candidateDistance);
				nearestDistance = candidateDistance;
			}
		}

		candidate = texture(auraDilatedX, vUV + vec2(0.0, float(distance)) * texel);

		if(distance != 0 && candidate.r > 0.5) {
			float candidateDistance = max(candidate.b, float(distance));

			if(candidateDistance < nearestDistance) {
				result = vec4(1.0, candidate.g, 0.0, candidateDistance);
				nearestDistance = candidateDistance;
			}
		}
	}

	fragColor = result;
}
)GLSL";

osg::ref_ptr<osg::Texture2D> makeTexture(
	int width,
	int height,
	GLint internalFormat,
	GLenum sourceFormat,
	GLenum sourceType
) {
	auto texture = osgx::make_ref<osg::Texture2D>();

	texture->setTextureSize(width, height);
	texture->setInternalFormat(internalFormat);
	texture->setSourceFormat(sourceFormat);
	texture->setSourceType(sourceType);
	texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	texture->setDataVariance(osg::Object::DYNAMIC);

	return texture;
}

struct DilationInput {
	osg::Texture2D* texture;
	const char* uniformName;
};

osg::ref_ptr<osg::Camera> makeDilationPass(
	const char* name,
	std::span<const DilationInput> inputs,
	osg::Texture2D* output,
	osg::Uniform* radius,
	const char* fragmentShader,
	int renderOrder
) {
	auto camera = osgx::make_ref<osg::Camera>();
	auto quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0)
	);
	auto geode = osgx::make_ref<osg::Geode>();
	auto program = osgx::make_ref<osg::Program>();

	geode->addDrawable(quad);
	program->setName(name);
	program->addShader(new osg::Shader(osg::Shader::VERTEX, osgx::FULLSCREEN_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragmentShader));
	camera->setName(name);
	camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	camera->setRenderOrder(osg::Camera::PRE_RENDER, renderOrder);
	camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	camera->setProjectionMatrix(osg::Matrix::identity());
	camera->setViewMatrix(osg::Matrix::identity());
	camera->setClearMask(GL_COLOR_BUFFER_BIT);
	camera->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	camera->setViewport(0, 0, output->getTextureWidth(), output->getTextureHeight());
	camera->attach(osg::Camera::COLOR_BUFFER0, output);
	camera->addChild(geode);

	auto* stateSet = camera->getOrCreateStateSet();

	stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
	stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

	for(std::size_t i = 0; i < inputs.size(); i++) {
		stateSet->setTextureAttributeAndModes(
			static_cast<unsigned int>(i), inputs[i].texture, osg::StateAttribute::ON
		);
		stateSet->addUniform(new osg::Uniform(inputs[i].uniformName, static_cast<int>(i)));
	}

	stateSet->addUniform(radius);

	return camera;
}

}

bool Aura::valid() const {
	return selectionCamera.valid()
		&& dilateXCamera.valid()
		&& dilateYCamera.valid()
		&& originalMask.valid()
		&& originalDepth.valid()
		&& dilatedX.valid()
		&& expanded.valid()
		&& radius.valid()
	;
}

Aura Aura::create(int width, int height, int radiusPixels) {
	Aura result;

	if(width <= 0 || height <= 0) return result;

	result.originalMask = makeTexture(width, height, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
	result.originalDepth = makeTexture(width, height, GL_R32F, GL_RED, GL_FLOAT);
	result.dilatedX = makeTexture(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
	result.expanded = makeTexture(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
	result.radius = new osg::Uniform("auraRadius", std::clamp(radiusPixels, 0, 64));

	auto program = osgx::make_ref<osg::Program>();

	program->setName("osgx_aura_SelectionMask");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, SELECTION_VERTEX_SHADER));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SELECTION_FRAGMENT_SHADER));

	result.selectionCamera = osgx::make_ref<osg::Camera>();
	result.selectionCamera->setName("osgx_aura_SelectionMask");
	result.selectionCamera->setRenderOrder(osg::Camera::PRE_RENDER, 1);
	result.selectionCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	result.selectionCamera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	result.selectionCamera->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	result.selectionCamera->setViewport(0, 0, width, height);
	result.selectionCamera->attach(osg::Camera::COLOR_BUFFER0, result.originalMask);
	result.selectionCamera->attach(osg::Camera::COLOR_BUFFER1, result.originalDepth);

	// A selected node commonly already owns its normal material Program. PROTECTED makes this
	// flat mask Program authoritative for this camera without mutating that visible scene state.
	result.selectionCamera->getOrCreateStateSet()->setAttributeAndModes(
		program, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED
	);

	const std::array dilateXInputs = {
		DilationInput{ result.originalMask, "auraOriginalMask" },
		DilationInput{ result.originalDepth, "auraOriginalDepth" }
	};
	const std::array dilateYInputs = {
		DilationInput{ result.dilatedX, "auraDilatedX" }
	};

	result.dilateXCamera = makeDilationPass(
		"osgx_aura_DilateX", dilateXInputs, result.dilatedX, result.radius, DILATE_X_FRAGMENT_SHADER, 2
	);
	result.dilateYCamera = makeDilationPass(
		"osgx_aura_DilateY", dilateYInputs, result.expanded, result.radius, DILATE_Y_FRAGMENT_SHADER, 3
	);

	return result;
}

}
