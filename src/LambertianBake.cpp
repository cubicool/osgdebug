#include "osgx/LambertianBake.hpp"
#include "osgx/IBL.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GL>
#include <osg/Group>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <string>

#ifndef GL_RGB16F
#  define GL_RGB16F 0x881B
#endif
#ifndef GL_COLOR_BUFFER_BIT
#  define GL_COLOR_BUFFER_BIT 0x00004000
#endif

namespace osgx::ibl {

namespace {

constexpr const char FULLSCREEN_VERT[] = R"GLSL(
#version 460 core
in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;
out vec2 vUV;
void main() {
	vUV = osg_MultiTexCoord0;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)GLSL";

constexpr const char LAMBERTIAN_FRAG[] = R"GLSL(
#version 460 core
const float PI = 3.14159265359;
uniform sampler2D equirectTex;
uniform int faceIndex;
uniform int sampleCount;
in vec2 vUV;
out vec4 fragColor;

float radicalInverseVdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

	return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint count) {
	return vec2(float(i) / float(count), radicalInverseVdC(i));
}

vec3 cubeDirection(int face, vec2 uv) {
	if(face == 0) return normalize(vec3( 1.0, -uv.y, -uv.x));
	if(face == 1) return normalize(vec3(-1.0, -uv.y,  uv.x));
	if(face == 2) return normalize(vec3( uv.x,  1.0,  uv.y));
	if(face == 3) return normalize(vec3( uv.x, -1.0, -uv.y));
	if(face == 4) return normalize(vec3( uv.x, -uv.y,  1.0));

	return normalize(vec3(-uv.x, -uv.y, -1.0));
}

vec3 glToZUp(vec3 direction) {
	return vec3(direction.x, -direction.z, direction.y);
}

vec2 equirectUV(vec3 direction) {
	float u = atan(direction.y, direction.x) / (2.0 * PI);
	float v = 1.0 - acos(clamp(direction.z, -1.0, 1.0)) / PI;

	return vec2(fract(u), v);
}

void main() {
	vec3 normal = glToZUp(cubeDirection(faceIndex, vUV * 2.0 - 1.0));
	vec3 up = abs(normal.z) > 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
	vec3 tangent = normalize(cross(up, normal));
	vec3 bitangent = cross(normal, tangent);
	vec3 irradiance = vec3(0.0);

	for(int i = 0; i < sampleCount; ++i) {
		vec2 xi = hammersley(uint(i), uint(sampleCount));
		float sinTheta = sqrt(xi.y);
		float cosTheta = sqrt(1.0 - xi.y);
		float phi = 2.0 * PI * xi.x;
		vec3 local = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
		vec3 direction = tangent * local.x + bitangent * local.y + normal * local.z;

		irradiance += texture(equirectTex, equirectUV(direction)).rgb;
	}

	fragColor = vec4(irradiance / float(sampleCount), 1.0);
}
)GLSL";

osg::ref_ptr<osg::Geode> makeFullscreenQuad() {
	auto quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1.0f, -1.0f, 0.0f),
		osg::Vec3(2.0f, 0.0f, 0.0f),
		osg::Vec3(0.0f, 2.0f, 0.0f)
	);
	auto geode = new osg::Geode();

	geode->addDrawable(quad);

	return geode;
}

osg::ref_ptr<osg::Program> makeProgram() {
	auto program = new osg::Program();

	program->setName("osgx_ibl_lambertianBake");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, FULLSCREEN_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, LAMBERTIAN_FRAG));

	return program;
}

void rearmBakeNodes(osg::Node* node) {
	if(!node) return;

	if(auto* callback = dynamic_cast<RunOnceCallback*>(node->getUpdateCallback())) callback->rebake(node);

	if(auto* group = node->asGroup()) {
		for(unsigned int child = 0; child < group->getNumChildren(); ++child) {
			rearmBakeNodes(group->getChild(child));
		}
	}
}

}

void LambertianBakeCompletion::operator()(osg::RenderInfo&) const {
	_done.store(true, std::memory_order_release);
}

bool LambertianBakeCompletion::done() const {
	return _done.load(std::memory_order_acquire);
}

void LambertianBakeCompletion::reset() {
	_done.store(false, std::memory_order_release);
}

bool LambertianBakeScene::ready() const {
	return completion && completion->done();
}

LambertianBakeScene createLambertianBakeScene(
	osg::Image* equirectangularHDR,
	const LambertianBakeOptions& options
) {
	LambertianBakeScene scene;

	if(!equirectangularHDR) return scene;

	const int cubeSize = std::max(options.cubeSize, 1);
	const int sampleCount = std::max(options.sampleCount, 1);
	auto sourceTexture = new osg::Texture2D();

	sourceTexture->setInternalFormat(GL_RGB16F);
	sourceTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	sourceTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	sourceTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	sourceTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	sourceTexture->setResizeNonPowerOfTwoHint(false);
	sourceTexture->setImage(equirectangularHDR);

	auto diffuseTexture = new osg::TextureCubeMap();

	diffuseTexture->setDataVariance(osg::Object::DYNAMIC);
	diffuseTexture->setTextureSize(cubeSize, cubeSize);
	diffuseTexture->setInternalFormat(GL_RGB16F);
	diffuseTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	diffuseTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	diffuseTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	diffuseTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	diffuseTexture->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);
	diffuseTexture->setUseHardwareMipMapGeneration(false);

	auto root = new osg::Group();
	auto bakeGroup = new osg::Group();
	auto program = makeProgram();
	auto quad = makeFullscreenQuad();
	osg::ref_ptr<osg::Camera> completionCamera;

	for(int face = 0; face < 6; ++face) {
		auto camera = new osg::Camera();

		camera->setName("osgx_LambertianBake_f" + std::to_string(face));
		camera->setRenderOrder(osg::Camera::PRE_RENDER, face);
		camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
		camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
		camera->setClearMask(GL_COLOR_BUFFER_BIT);
		camera->setViewport(0, 0, cubeSize, cubeSize);
		camera->setProjectionMatrix(osg::Matrix::identity());
		camera->setViewMatrix(osg::Matrix::identity());
		camera->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);
		camera->setCullingMode(osg::Camera::NO_CULLING);
		camera->attach(
			osg::Camera::COLOR_BUFFER0,
			diffuseTexture,
			0,
			static_cast<unsigned int>(face),
			false
		);
		camera->setUpdateCallback(new RunOnceCallback());

		auto* stateSet = camera->getOrCreateStateSet();

		stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
		stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
		stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
		stateSet->addUniform(new osg::Uniform("equirectTex", 0));
		stateSet->addUniform(new osg::Uniform("faceIndex", face));
		stateSet->addUniform(new osg::Uniform("sampleCount", sampleCount));
		stateSet->setTextureAttributeAndModes(0, sourceTexture, osg::StateAttribute::ON);
		camera->addChild(quad);
		bakeGroup->addChild(camera);

		if(face == 5) completionCamera = camera;
	}

	auto completion = new LambertianBakeCompletion();

	// The face cameras have explicit PRE_RENDER order, so this post-draw callback cannot run until
	// every output face has been populated. Keeping it on a real FBO pass avoids a dummy camera
	// whose callback timing would depend on a renderer accepting an attachment-less target.
	completionCamera->setPostDrawCallback(completion);
	root->addChild(bakeGroup);

	scene.root = root;
	scene.sourceTexture = sourceTexture;
	scene.diffuseTexture = diffuseTexture;
	scene.completion = completion;

	return scene;
}

bool rebakeLambertianBakeScene(
	LambertianBakeScene& scene,
	osg::Image* equirectangularHDR
) {
	if(!scene.root || !scene.sourceTexture || !scene.completion || !equirectangularHDR) return false;

	scene.sourceTexture->setImage(equirectangularHDR);
	scene.completion->reset();
	rearmBakeNodes(scene.root);

	return true;
}

}
