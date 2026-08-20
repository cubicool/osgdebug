// vimrun! ./examples/osgx-shadow
//
// A standalone proof that osgx::shadow::DIRECT_LIGHTING_HOOK_SHADOWED actually shadows: three
// osgx::Cube shapes of different sizes/colors sitting on a flat floor quad, lit by one directional
// osgx::pbr::LightSet light, its shadow cast via osgx::shadow::createDirectionalShadowMap().
//
// Deliberately NOT osgx::gltf::pbribl -- no glTF asset, no IBL environment, nothing but the
// generic osgx::pbr direct-lighting hook contract plus the new shadow one, mirroring
// osgx-lights.cpp's own "load nothing, just press a key" shape as closely as possible: the
// fragment shader here is IDENTICAL to osgx-lights.cpp's (only DIRECT_LIGHTING_DECL + a call
// site) -- the only difference is which hook shader object makeProgram() adds alongside it
// (DIRECT_LIGHTING_HOOK_SHADOWED instead of DIRECT_LIGHTING_HOOK_DEFAULT) and the extra
// shadow-map texture/uniforms wired onto the StateSet. That's the whole point: proving the hook
// swap is really a drop-in, no other shader change needed.
//
// Scene-graph shape (avoids a shadow-texture read/write feedback loop, same pattern the old
// hand-rolled pyosg-lighting/08-shadows.py used): root -> [shadowMap.camera (PRE_RENDER, renders
// ONLY the three cubes into the depth texture) , mainGroup (shadow texture + shadow/light
// uniforms; renders the cubes AND the floor, lit+shadowed)].
//
// Press 's' to toggle the shadow on/off (swaps back to the unshadowed hook shader) -- the
// clearest possible A/B: same scene, same light, only the shadow term changes.

#include "osgx/osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GL>
#include <osg/Group>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>
#include <osg/Uniform>

#include <osgGA/TrackballManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <iostream>
#include <string_view>

namespace {

// Same attribute layout osgx::Cube uses (Shapes.hpp's VertexLayout default: position=0,
// normal=1) -- the floor quad below is built by hand to match, so both it and the cubes work
// with the exact same Program.
constexpr std::string_view VERTEX_SHADER = R"GLSL(
#version 460 core

in vec3 position;
in vec3 normal;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;

out vec3 vNormal;
out vec3 vPosition;

void main() {
	vNormal = osg_NormalMatrix * normal;
	vPosition = (osg_ModelViewMatrix * vec4(position, 1.0)).xyz;
	gl_Position = osg_ModelViewProjectionMatrix * vec4(position, 1.0);
}
)GLSL";

// Identical to osgx-lights.cpp's FRAGMENT_SHADER -- see this file's header comment for why that's
// the whole point. Only needs osgx_DirectLighting()'s CONTRACT declaration + a call site; whether
// that call is shadowed or not is entirely decided by which hook shader object makeProgram()
// below adds alongside this one.
constexpr std::string_view FRAGMENT_SHADER = R"GLSL(
#version 460 core

const float PI = 3.14159265359;

#pragma osgx::pbr MATERIAL_STRUCT, DIRECT_LIGHTING_DECL

in vec3 vNormal;
in vec3 vPosition;

uniform mat4 osg_ViewMatrix;
uniform mat4 osg_ViewMatrixInverse;

uniform vec3 albedo;
uniform float roughness;
uniform float metallic;
uniform vec3 ambientColor;
uniform float ambientIntensity;

out vec4 fragColor;

void main() {
	osgx_Material mat;

	mat.albedo = albedo;
	mat.ao = 1.0;
	mat.roughness = roughness;
	mat.metallic = metallic;
	mat.F0 = mix(vec3(0.04), albedo, metallic);

	mat3 invViewRot = transpose(mat3(osg_ViewMatrix));
	vec3 N = invViewRot * normalize(vNormal);
	vec3 V = invViewRot * normalize(-vPosition);
	vec3 worldPos = (osg_ViewMatrixInverse * vec4(vPosition, 1.0)).xyz;

	vec3 color = ambientColor * ambientIntensity * mat.albedo * mat.ao;

	color += osgx_DirectLighting(N, V, worldPos, mat);

	color = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));

	fragColor = vec4(color, 1.0);
}
)GLSL";

// `shadowed` selects DIRECT_LIGHTING_HOOK_SHADOWED vs. plain DIRECT_LIGHTING_HOOK_DEFAULT -- the
// 's'-key toggle in main() rebuilds the Program via this same function, swapping only that one
// shader object.
osg::ref_ptr<osg::Program> makeProgram(bool shadowed) {
	osgx::pbr::registerShaderLibs();
	osgx::shadow::registerShaderLibs();

	auto program = osgx::make_ref<osg::Program>();
	auto fragmentSrc = osgx::resolveShaderLibs(std::string(FRAGMENT_SHADER));
	auto hookSrc = osgx::resolveShaderLibs(std::string(
		shadowed ? osgx::shadow::DIRECT_LIGHTING_HOOK_SHADOWED : osgx::pbr::DIRECT_LIGHTING_HOOK_DEFAULT
	));

	program->setName(shadowed ? "osgx_shadow_demo_shadowed" : "osgx_shadow_demo_unshadowed");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, std::string(VERTEX_SHADER)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragmentSrc));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, hookSrc));
	program->addBindAttribLocation("position", 0);
	program->addBindAttribLocation("normal", 1);

	return program;
}

// Trivial fragment shader for the shadow camera's depth-only pass: a core-profile context has no
// fixed-function fallback, so the casters below still need SOME bound Program to produce
// gl_Position even though nothing they write to color matters (the shadow camera's drawBuffer is
// GL_NONE). Reuses VERTEX_SHADER unchanged; the fragment shader here does nothing on purpose.
constexpr std::string_view DEPTH_ONLY_FRAGMENT_SHADER = R"GLSL(
#version 460 core
void main() {}
)GLSL";

osg::ref_ptr<osg::Program> makeDepthOnlyProgram() {
	auto program = osgx::make_ref<osg::Program>();

	program->setName("osgx_shadow_demo_depthOnly");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, std::string(VERTEX_SHADER)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, std::string(DEPTH_ONLY_FRAGMENT_SHADER)));
	program->addBindAttribLocation("position", 0);
	program->addBindAttribLocation("normal", 1);

	return program;
}

// A flat floor quad, XY plane at the given Z, built with the same vertex-attribute layout
// osgx::Cube uses (position=0, normal=1) so it renders through the identical Program the cubes
// use -- no separate floor shader needed, unlike the old hand-rolled pyosg-lighting examples.
osg::ref_ptr<osg::Geode> makeFloor(float halfSize, float z) {
	auto positions = osgx::make_ref<osg::Vec3Array>();

	positions->push_back(osg::Vec3(-halfSize, -halfSize, z));
	positions->push_back(osg::Vec3(halfSize, -halfSize, z));
	positions->push_back(osg::Vec3(halfSize, halfSize, z));
	positions->push_back(osg::Vec3(-halfSize, halfSize, z));

	auto normals = osgx::make_ref<osg::Vec3Array>();

	normals->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));

	auto geometry = osgx::make_ref<osg::Geometry>();

	geometry->setVertexArray(positions.get());
	geometry->setNormalArray(normals.get(), osg::Array::BIND_OVERALL);
	geometry->setVertexAttribArray(0, positions.get(), osg::Array::BIND_PER_VERTEX);
	geometry->setVertexAttribArray(1, normals.get(), osg::Array::BIND_OVERALL);
	// GL_TRIANGLE_FAN, not GL_QUADS -- GL_QUADS is removed in a core-profile context (this project's
	// shaders are all "#version 460 core"); a 4-vertex fan is exactly the same two triangles for a
	// convex quad and works in either profile.
	geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4));

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(geometry.get());

	return geode;
}

osg::ref_ptr<osg::Geode> makeCube(const osg::Vec3& center, const osg::Vec3& size) {
	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(new osgx::Cube(center, size));

	return geode;
}

}

int main() {
	// Three cubes of different footprints/heights, loosely matching osgx-grid's own floor demo
	// screenshot -- close enough to prove multi-caster shadows land in believable places, not a
	// pixel-exact match. Sizes are (width, depth, height); center.z is size.z()/2 so each cube's
	// base sits exactly on the floor (z=0).
	struct CubeSpec {
		osg::Vec3 center;
		osg::Vec3 size;
		osg::Vec3 color;
	};

	const CubeSpec cubes[] = {
		{osg::Vec3(-1.3f, 0.0f, 0.5f), osg::Vec3(1.0f, 1.0f, 1.0f), osg::Vec3(0.90f, 0.25f, 0.20f)}, // red
		{osg::Vec3(0.3f, 0.4f, 0.75f), osg::Vec3(0.9f, 0.9f, 1.5f), osg::Vec3(0.20f, 0.55f, 0.90f)}, // blue
		{osg::Vec3(0.5f, -0.7f, 0.35f), osg::Vec3(0.7f, 0.7f, 0.7f), osg::Vec3(0.95f, 0.75f, 0.10f)}, // yellow
	};

	// Directional light travel direction (down and across) -- steep enough that all three cubes
	// cast a clearly visible shadow onto the floor without one cube's shadow completely burying
	// another's.
	const osg::Vec3 lightDir = osg::Vec3(0.5f, 0.35f, -1.0f);
	const osg::Vec3 sceneBoundCenter(0.0f, 0.0f, 0.5f);
	constexpr float sceneBoundRadius = 2.2f;

	auto root = osgx::make_ref<osg::Group>();
	auto casters = osgx::make_ref<osg::Group>();
	auto mainGroup = osgx::make_ref<osg::Group>();
	auto floor = makeFloor(6.0f, 0.0f);

	for(const auto& spec: cubes) {
		auto caster = makeCube(spec.center, spec.size);
		auto receiver = makeCube(spec.center, spec.size);

		receiver->getOrCreateStateSet()->addUniform(new osg::Uniform("albedo", spec.color));

		casters->addChild(caster.get());
		mainGroup->addChild(receiver.get());
	}

	mainGroup->addChild(floor.get());

	auto* mainSS = mainGroup->getOrCreateStateSet();

	mainSS->addUniform(new osg::Uniform("roughness", 0.6f));
	mainSS->addUniform(new osg::Uniform("metallic", 0.0f));
	mainSS->addUniform(new osg::Uniform("ambientColor", osg::Vec3(1.0f, 1.0f, 1.0f)));
	mainSS->addUniform(new osg::Uniform("ambientIntensity", 0.08f));
	// Floor never sets its own "albedo" (unlike the cubes above) -- a flat, slightly warm
	// gray-stone default so it reads clearly against the shadow it receives.
	floor->getOrCreateStateSet()->addUniform(new osg::Uniform("albedo", osg::Vec3(0.72f, 0.68f, 0.60f)));

	auto lights = osgx::pbr::LightSet::create(mainSS);

	lights.setCount(1);
	lights.setDirectional(0, lightDir, osg::Vec3(1.0f, 0.96f, 0.88f), 3.0f);

	osgx::shadow::ShadowMapOptions shadowOptions;

	auto shadowMap = osgx::shadow::createDirectionalShadowMap(
		lightDir, sceneBoundCenter, sceneBoundRadius, shadowOptions
	);

	casters->getOrCreateStateSet()->setAttributeAndModes(
		makeDepthOnlyProgram().get(), osg::StateAttribute::ON
	);
	shadowMap.camera->addChild(casters.get());

	// Shadow texture unit 0 -- this demo has no other textures. `shadowMap`'s own bias/strength/
	// casterIndex uniforms are added as-is (their defaults already match ShadowMapOptions above).
	mainSS->setTextureAttributeAndModes(0, shadowMap.depthTexture.get(), osg::StateAttribute::ON);
	mainSS->addUniform(new osg::Uniform("osgx_shadowMap", 0));
	mainSS->addUniform(shadowMap.shadowMatrix.get());
	mainSS->addUniform(shadowMap.bias.get());
	mainSS->addUniform(shadowMap.strength.get());
	mainSS->addUniform(shadowMap.casterIndex.get());

	bool shadowed = true;

	mainSS->setAttributeAndModes(makeProgram(shadowed).get(), osg::StateAttribute::ON);

	root->addChild(shadowMap.camera.get());
	root->addChild(mainGroup.get());

	std::cout << "osgx-shadow: shadow ON (press 's' to toggle)" << std::endl;

	auto viewer = osgViewer::Viewer();

	viewer.addEventHandler(new osgx::LambdaKeyHandler('s', [mainSS, &shadowed](auto&, auto&) {
		shadowed = !shadowed;

		mainSS->setAttributeAndModes(makeProgram(shadowed).get(), osg::StateAttribute::ON);

		std::cout << "osgx-shadow: shadow " << (shadowed ? "ON" : "OFF") << std::endl;

		return true;
	}));

	viewer.setSceneData(root.get());

	auto* manip = new osgGA::TrackballManipulator();

	viewer.setCameraManipulator(manip);
	manip->setHomePosition(osg::Vec3(4.5, -5.5, 3.5), osg::Vec3(0.0, 0.0, 0.5), osg::Vec3(0.0, 0.0, 1.0));
	manip->home(0.0);
	viewer.getCamera()->setClearColor(osg::Vec4(0.04f, 0.05f, 0.08f, 1.0f));
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
