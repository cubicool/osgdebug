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
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
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
	DisplayModeHandler(osg::Switch* display, bool panoramaAvailable):
		_display(display),
		_panoramaAvailable(panoramaAvailable) {}

	bool handle(const osgGA::GUIEventAdapter& event, osgGA::GUIActionAdapter&) override {
		if(event.getEventType() != osgGA::GUIEventAdapter::KEYDOWN) return false;

		if(event.getKey() == 'p' && _panoramaAvailable) {
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
		_display->setValue(2, _panoramaAvailable && _panorama);
	}

	osg::ref_ptr<osg::Switch> _display;
	bool _panoramaAvailable = false;
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

std::string replaceFaceToken(std::string_view pattern, std::size_t face) {
	constexpr std::string_view token = "{face}";
	const auto position = pattern.find(token);

	if(position == std::string_view::npos) return {};

	std::string result(pattern);

	result.replace(position, token.size(), std::to_string(face));

	return result;
}

osg::ref_ptr<osg::TextureCubeMap> loadRawCubemapRGBA32F(std::string_view pattern, int size) {
	static constexpr std::array faces = {
		osg::TextureCubeMap::POSITIVE_X,
		osg::TextureCubeMap::NEGATIVE_X,
		osg::TextureCubeMap::POSITIVE_Y,
		osg::TextureCubeMap::NEGATIVE_Y,
		osg::TextureCubeMap::POSITIVE_Z,
		osg::TextureCubeMap::NEGATIVE_Z
	};
	const auto pixelCount = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
	const auto byteCount = pixelCount * 4 * sizeof(float);
	auto cubemap = osgx::make_ref<osg::TextureCubeMap>();

	for(std::size_t face = 0; face < faces.size(); ++face) {
		const std::filesystem::path filename(replaceFaceToken(pattern, face));
		std::error_code error;
		const auto actualBytes = std::filesystem::file_size(filename, error);

		if(error || actualBytes != byteCount) {
			std::cerr << "osgx-ibl: expected " << byteCount << " bytes in '" << filename.string()
				<< "', got " << (error ? 0 : actualBytes) << std::endl;

			return {};
		}

		auto image = osgx::make_ref<osg::Image>();

		image->allocateImage(size, size, 1, GL_RGBA, GL_FLOAT);

		std::ifstream input(filename, std::ios::binary);

		input.read(reinterpret_cast<char*>(image->data()), static_cast<std::streamsize>(byteCount));

		if(!input) {
			std::cerr << "osgx-ibl: failed to read '" << filename.string() << "'" << std::endl;

			return {};
		}

		cubemap->setImage(faces[face], image);
	}

	cubemap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	cubemap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	cubemap->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	cubemap->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	cubemap->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);
	cubemap->setUseHardwareMipMapGeneration(false);

	return cubemap;
}

}

int main(int argc, char** argv) {
	std::string_view hdrPath;
	std::string_view rawCubemapPattern;
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

		else if(argument == "--raw-cubemap-rgba32f") {
			if(index + 1 == argc) {
				std::cerr << "osgx-ibl: --raw-cubemap-rgba32f requires a {face} filename pattern" << std::endl;

				return 1;
			}

			rawCubemapPattern = argv[++index];
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

	if((hdrPath.empty() && rawCubemapPattern.empty()) || (!hdrPath.empty() && !rawCubemapPattern.empty())) {
		std::cerr
			<< "Usage: osgx-ibl <environment.hdr> [--mode lambertian] [--size N] [--samples N]" << std::endl
			<< "   or: osgx-ibl --raw-cubemap-rgba32f '<prefix>{face}<suffix>' [--size N]" << std::endl
			<< "Controls: 'c' toggles diffuse skybox/cross; 'p' toggles the raw HDR panorama" << std::endl;

		return 1;
	}

	osg::ref_ptr<osg::TextureCubeMap> cubemap;
	osg::ref_ptr<osg::Group> bakeRoot;
	osg::ref_ptr<osgx::ibl::BakeCompletion> completion;
	osg::ref_ptr<osg::Texture2D> sourceTexture;
	const bool isRawCubemap = !rawCubemapPattern.empty();

	if(isRawCubemap) {
		cubemap = loadRawCubemapRGBA32F(rawCubemapPattern, cubeSize);

		if(!cubemap) return 1;
	}

	else {
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

		cubemap = bake.diffuseTexture;
		bakeRoot = bake.root;
		completion = bake.completion;
		sourceTexture = bake.sourceTexture;
	}

	auto display = new osg::Switch();

	display->addChild(makeSkybox(*cubemap), true);
	display->addChild(makeCross(*cubemap), false);
	if(sourceTexture) display->addChild(makePanorama(*sourceTexture), false);
	else display->addChild(new osg::Group(), false);

	auto root = new osg::Group();

	if(bakeRoot) root->addChild(bakeRoot);
	root->addChild(display);
	if(completion) root->setUpdateCallback(new BakeStatusCallback(completion));

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
	viewer.addEventHandler(new DisplayModeHandler(display, sourceTexture.valid()));
	viewer.addEventHandler(new ScrollBlocker());
	viewer.home();

	if(isRawCubemap) {
		std::cout << "osgx-ibl: raw RGBA32F cubemap " << cubeSize << "x" << cubeSize << std::endl
			<< "Controls: 'c' toggles cubemap skybox/cross" << std::endl;
	}

	else {
		std::cout
			<< "osgx-ibl: GPU Lambertian bake " << cubeSize << "x" << cubeSize
			<< ", " << sampleCount << " samples" << std::endl
			<< "Controls: 'c' toggles diffuse skybox/cross; 'p' toggles the raw HDR panorama" << std::endl;
	}

	return viewer.run();
}
