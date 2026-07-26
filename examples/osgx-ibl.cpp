// vimrun! ./examples/osgx-ibl Cannon_Exterior.hdr
//
// Generic IBL inspection tool. It intentionally knows about HDR input and osgx bake products,
// but not glTF, KTX2, or any application-specific material setup.

#include <osgx/LambertianBake.hpp>
#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/GL>
#include <osg/Array>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/NodeCallback>
#include <osg/NodeVisitor>
#include <osg/Program>
#include <osg/PrimitiveSet>
#include <osg/Shader>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osg/Switch>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/Uniform>
#include <osg/Vec3d>
#include <osgDB/ReadFile>
#include <osgGA/GUIEventHandler>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr const char SKYBOX_VERT[] = R"GLSL(
#version 460 core
in vec4 osg_Vertex;
uniform mat4 osg_ModelViewProjectionMatrix;
out vec3 vDirection;
void main() {
	vDirection = osg_Vertex.xyz;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)GLSL";

constexpr const char SKYBOX_FRAG[] = R"GLSL(
#version 460 core
uniform samplerCube environment;
in vec3 vDirection;
out vec4 fragColor;
void main() {
	// The Lambertian baker receives Z-up world directions and stores them in ordinary GL cube-face
	// layout. Apply the matching Z-up -> GL lookup transform used by the PBR shader.
	vec3 cubeDirection = normalize(vec3(vDirection.x, vDirection.z, -vDirection.y));
	vec3 color = texture(environment, cubeDirection).rgb;
	color = color / (color + vec3(1.0));
	fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

constexpr const char CROSS_VERT[] = R"GLSL(
#version 460 core
in vec4 osg_Vertex;
layout(location = 1) in vec3 cubeDirection;
out vec3 vDirection;
void main() {
	vDirection = cubeDirection;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)GLSL";

constexpr const char CROSS_FRAG[] = R"GLSL(
#version 460 core
uniform samplerCube environment;
in vec3 vDirection;
out vec4 fragColor;
void main() {
	vec3 color = texture(environment, normalize(vDirection)).rgb;
	color = color / (color + vec3(1.0));
	fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

constexpr const char PANORAMA_VERT[] = R"GLSL(
#version 460 core
in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;
out vec2 vUV;
void main() {
	vUV = osg_MultiTexCoord0;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)GLSL";

constexpr const char PANORAMA_FRAG[] = R"GLSL(
#version 460 core
uniform sampler2D panorama;
in vec2 vUV;
out vec4 fragColor;
void main() {
	vec3 color = texture(panorama, vUV).rgb;
	color = color / (color + vec3(1.0));
	fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// Horizontal GL cubemap cross. Unlike the skybox, this view uses literal GL cube directions so
// each face's placement/orientation can be inspected independently of the renderer's Z-up world.
osg::ref_ptr<osg::Geometry> makeCubeCross() {
	auto geometry = new osg::Geometry();
	auto vertices = new osg::Vec3Array();
	auto directions = new osg::Vec3Array();
	auto indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

	constexpr float halfCellWidth = 0.25f;
	constexpr float halfCellHeight = 1.0f / 3.0f;

	struct Face {
		float x;
		float y;
		osg::Vec3 directions[4]; // bottom-left, bottom-right, top-right, top-left
	};

	const Face faces[] = {
		{-0.25f,  2.0f / 3.0f, {{-1, 1,-1}, { 1, 1,-1}, { 1, 1, 1}, {-1, 1, 1}}}, // +Y
		{-0.75f,  0.0f,        {{-1, 1,-1}, {-1, 1, 1}, {-1,-1, 1}, {-1,-1,-1}}}, // -X
		{-0.25f,  0.0f,        {{-1, 1, 1}, { 1, 1, 1}, { 1,-1, 1}, {-1,-1, 1}}}, // +Z
		{ 0.25f,  0.0f,        {{ 1, 1, 1}, { 1, 1,-1}, { 1,-1,-1}, { 1,-1, 1}}}, // +X
		{ 0.75f,  0.0f,        {{ 1, 1,-1}, {-1, 1,-1}, {-1,-1,-1}, { 1,-1,-1}}}, // -Z
		{-0.25f, -2.0f / 3.0f, {{-1,-1, 1}, { 1,-1, 1}, { 1,-1,-1}, {-1,-1,-1}}}  // -Y
	};

	for(const auto& face : faces) {
		const auto base = static_cast<unsigned int>(vertices->size());

		vertices->push_back({face.x - halfCellWidth, face.y - halfCellHeight, 0.0f});
		vertices->push_back({face.x + halfCellWidth, face.y - halfCellHeight, 0.0f});
		vertices->push_back({face.x + halfCellWidth, face.y + halfCellHeight, 0.0f});
		vertices->push_back({face.x - halfCellWidth, face.y + halfCellHeight, 0.0f});

		for(const auto& direction : face.directions) directions->push_back(direction);

		indices->push_back(base + 0); indices->push_back(base + 1); indices->push_back(base + 2);
		indices->push_back(base + 0); indices->push_back(base + 2); indices->push_back(base + 3);
	}

	geometry->setVertexArray(vertices);
	geometry->setVertexAttribArray(1, directions);
	geometry->setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
	geometry->addPrimitiveSet(indices);

	return geometry;
}

void configureCubeDisplay(
	osg::StateSet& stateSet,
	osg::Program& program,
	osg::TextureCubeMap& texture
) {
	stateSet.setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	stateSet.setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	stateSet.setMode(GL_TEXTURE_CUBE_MAP_SEAMLESS, osg::StateAttribute::ON);
	stateSet.setAttributeAndModes(&program, osg::StateAttribute::ON);
	stateSet.setTextureAttributeAndModes(0, &texture, osg::StateAttribute::ON);
	stateSet.addUniform(new osg::Uniform("environment", 0));
}

osg::ref_ptr<osg::Geode> makeSkybox(osg::TextureCubeMap& texture) {
	auto geode = new osg::Geode();
	auto program = new osg::Program();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, SKYBOX_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SKYBOX_FRAG));
	geode->addDrawable(new osg::ShapeDrawable(new osg::Box(osg::Vec3(), 200.0f)));
	configureCubeDisplay(*geode->getOrCreateStateSet(), *program, texture);

	return geode;
}

osg::ref_ptr<osg::Geode> makeCross(osg::TextureCubeMap& texture) {
	auto geode = new osg::Geode();
	auto program = new osg::Program();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, CROSS_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, CROSS_FRAG));
	program->addBindAttribLocation("cubeDirection", 1);
	geode->addDrawable(makeCubeCross());
	configureCubeDisplay(*geode->getOrCreateStateSet(), *program, texture);

	return geode;
}

osg::ref_ptr<osg::Geode> makePanorama(osg::Texture2D& texture) {
	auto geode = new osg::Geode();
	auto program = new osg::Program();
	auto quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1.0f, -1.0f, 0.0f),
		osg::Vec3(2.0f, 0.0f, 0.0f),
		osg::Vec3(0.0f, 2.0f, 0.0f)
	);

	program->addShader(new osg::Shader(osg::Shader::VERTEX, PANORAMA_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, PANORAMA_FRAG));
	geode->addDrawable(quad);

	auto* stateSet = geode->getOrCreateStateSet();

	stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
	stateSet->setTextureAttributeAndModes(0, &texture, osg::StateAttribute::ON);
	stateSet->addUniform(new osg::Uniform("panorama", 0));

	return geode;
}

class DisplayModeHandler final: public osgGA::GUIEventHandler {
public:
	explicit DisplayModeHandler(osg::Switch* display): _display(display) {}

	bool handle(const osgGA::GUIEventAdapter& event, osgGA::GUIActionAdapter&) override {
		if(event.getEventType() != osgGA::GUIEventAdapter::KEYDOWN) return false;

		if(event.getKey() == 'p') {
			_panorama = !_panorama;
			_apply();
			std::cout << "osgx-ibl: mode " << (_panorama ? "panorama" : (_cross ? "cross" : "skybox")) << std::endl;

			return true;
		}

		if(event.getKey() != 'c') return false;

		_cross = !_cross;
		_panorama = false;
		_apply();
		std::cout << "osgx-ibl: mode " << (_cross ? "cross" : "skybox") << std::endl;

		return true;
	}

private:
	void _apply() {
		_display->setValue(0, !_panorama && !_cross);
		_display->setValue(1, !_panorama && _cross);
		_display->setValue(2, _panorama);
	}

	osg::ref_ptr<osg::Switch> _display;
	bool _cross = false;
	bool _panorama = false;
};

// A Lambertian irradiance cube has one level only. Until a future mode exposes a meaningful wheel
// action (for example, GGX mip selection), consume scroll events so TrackballManipulator cannot
// dolly the viewer outside the enclosing skybox.
class ScrollBlocker final: public osgGA::GUIEventHandler {
public:
	bool handle(const osgGA::GUIEventAdapter& event, osgGA::GUIActionAdapter&) override {
		return event.getEventType() == osgGA::GUIEventAdapter::SCROLL;
	}
};

class BakeStatusCallback final: public osg::NodeCallback {
public:
	explicit BakeStatusCallback(osgx::ibl::BakeCompletion* completion): _completion(completion) {}

	void operator()(osg::Node* node, osg::NodeVisitor* visitor) override {
		if(!_reported && _completion && _completion->done()) {
			std::cout << "osgx-ibl: Lambertian bake complete" << std::endl;
			_reported = true;
		}

		traverse(node, visitor);
	}

private:
	osg::ref_ptr<osgx::ibl::BakeCompletion> _completion;
	bool _reported = false;
};

bool parsePositiveInt(std::string_view text, int& result) {
	int parsed = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);

	if(error != std::errc{} || end != text.data() + text.size() || parsed < 1) return false;

	result = parsed;

	return true;
}

}

int main(int argc, char** argv) {
	std::string_view hdrPath;
	int cubeSize = 256;
	int sampleCount = 2048;

	for(int index = 1; index < argc; index++) {
		const std::string_view argument(argv[index]);

		if(argument == "--mode") {
			if(index + 1 == argc || std::string_view(argv[index + 1]) != "lambertian") {
				std::cerr << "osgx-ibl: only '--mode lambertian' is available currently" << std::endl;

				return 1;
			}

			index++;
		}

		else if(argument == "--size" || argument == "--samples") {
			if(index + 1 == argc) {
				std::cerr << "osgx-ibl: " << argument << " requires a positive integer" << std::endl;

				return 1;
			}

			index++;

			int value = 0;

			if(!parsePositiveInt(argv[index], value)) {
				std::cerr << "osgx-ibl: " << argument << " requires a positive integer" << std::endl;

				return 1;
			}

			if(argument == "--size") cubeSize = value;
			else sampleCount = value;
		}

		else if(argument.starts_with("--")) {
			std::cerr << "osgx-ibl: unknown option " << argument << std::endl;

			return 1;
		}

		else if(hdrPath.empty()) hdrPath = argument;
		else {
			std::cerr << "osgx-ibl: only one HDR input is supported" << std::endl;

			return 1;
		}
	}

	if(hdrPath.empty()) {
		std::cerr
			<< "Usage: osgx-ibl <environment.hdr> [--mode lambertian] [--size N] [--samples N]" << std::endl
			<< "Controls: 'c' toggles diffuse skybox/cross; 'p' toggles the raw HDR panorama" << std::endl;

		return 1;
	}

	auto hdrImage = osgDB::readRefImageFile(std::string(hdrPath));

	if(!hdrImage) {
		std::cerr << "osgx-ibl: failed to load HDR image " << hdrPath << std::endl;

		return 1;
	}

	auto bake = osgx::ibl::createLambertianBakeScene(
		hdrImage,
		{.cubeSize = cubeSize, .sampleCount = sampleCount}
	);

	if(!bake.root || !bake.diffuseTexture || !bake.completion) {
		std::cerr << "osgx-ibl: could not create the Lambertian bake scene" << std::endl;

		return 1;
	}

	auto display = new osg::Switch();

	display->addChild(makeSkybox(*bake.diffuseTexture), true);
	display->addChild(makeCross(*bake.diffuseTexture), false);
	display->addChild(makePanorama(*bake.sourceTexture), false);

	auto root = new osg::Group();

	root->addChild(bake.root);
	root->addChild(display);
	root->setUpdateCallback(new BakeStatusCallback(bake.completion));

	osgViewer::Viewer viewer;
	auto manipulator = new osgGA::TrackballManipulator();

	manipulator->setHomePosition(
		osg::Vec3d(0.0, 0.0, 0.0),
		osg::Vec3d(0.0, 1.0, 0.0),
		osg::Vec3d(0.0, 0.0, 1.0)
	);
	viewer.setCameraManipulator(manipulator);
	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new DisplayModeHandler(display));
	viewer.addEventHandler(new ScrollBlocker());
	viewer.home();

	std::cout
		<< "osgx-ibl: GPU Lambertian bake " << cubeSize << "x" << cubeSize
		<< ", " << sampleCount << " samples" << std::endl
		<< "Controls: 'c' toggles diffuse skybox/cross; 'p' toggles the raw HDR panorama" << std::endl;

	return viewer.run();
}
