// vimrun! ./examples/osgx-turntable floor
//
// A deliberately small staging ground for the turntable viewer. `floor` enables the procedural
// infinite grid from osgx-grid.cpp; the colored cubes are temporary stand-ins for a real model.

#include "../osgx/Core.hpp"
#include "../osgx/Cursor.hpp"
#include "../osgx/Manipulators.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/CullSettings>
#include <osg/Depth>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osgGA/GUIEventHandler>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <iostream>
#include <string>

namespace {

constexpr const char SKY_VERTEX_SHADER[] = R"GLSL(
#version 330 core

uniform mat4 osg_ProjectionMatrix;
uniform mat4 osg_ViewMatrixInverse;

in vec4 osg_Vertex;

out vec3 worldDirection;

void main() {
	vec4 viewDirection = inverse(osg_ProjectionMatrix) * vec4(osg_Vertex.xy, 1.0, 1.0);

	worldDirection = normalize(mat3(osg_ViewMatrixInverse) * viewDirection.xyz);
	gl_Position = osg_Vertex;
}
)GLSL";

constexpr const char SKY_FRAGMENT_SHADER[] = R"GLSL(
#version 330 core

in vec3 worldDirection;

out vec4 fragColor;

void main() {
	const vec3 horizonBlue = vec3(0.055, 0.130, 0.275);
	const vec3 horizonSteel = vec3(0.105, 0.185, 0.315);
	const vec3 horizonViolet = vec3(0.180, 0.075, 0.250);
	const vec3 horizonCrimson = vec3(0.200, 0.060, 0.150);
	const vec3 zenith = vec3(0.002, 0.005, 0.016);
	const vec3 belowHorizon = vec3(0.001, 0.002, 0.006);
	float elevation = clamp(worldDirection.z, -1.0, 1.0);
	float azimuth = atan(worldDirection.y, worldDirection.x) / (2.0 * 3.14159265) + 0.5;
	float region = azimuth * 4.0;
	float blend = smoothstep(0.0, 1.0, fract(region));
	float sky = smoothstep(-0.08, 0.16, elevation);
	float zenithBlend = smoothstep(0.02, 0.65, elevation);
	vec3 horizon;

	if(region < 1.0) horizon = mix(horizonBlue, horizonSteel, blend);
	else if(region < 2.0) horizon = mix(horizonSteel, horizonViolet, blend);
	else if(region < 3.0) horizon = mix(horizonViolet, horizonCrimson, blend);
	else horizon = mix(horizonCrimson, horizonBlue, blend);

	vec3 color = mix(belowHorizon, horizon, sky);

	color = mix(color, zenith, zenithBlend);

	// A little atmospheric glow at the horizon keeps the grid from disappearing into a hard void.
	color += vec3(0.025, 0.040, 0.075) * exp(-22.0 * abs(elevation));

	fragColor = vec4(color, 1.0);
}
)GLSL";

constexpr const char INFINITE_FLOOR_VERTEX_SHADER[] = R"GLSL(
#version 330 core

uniform mat4 osg_ProjectionMatrix;
uniform mat4 osg_ViewMatrixInverse;

in vec4 osg_Vertex;

out vec3 nearPoint;
out vec3 farPoint;

vec3 unproject(vec2 ndc, float depth) {
	vec4 view = inverse(osg_ProjectionMatrix) * vec4(ndc, depth, 1.0);

	view /= view.w;

	vec4 world = osg_ViewMatrixInverse * view;

	return world.xyz / world.w;
}

void main() {
	nearPoint = unproject(osg_Vertex.xy, -1.0);
	farPoint = unproject(osg_Vertex.xy, 1.0);
	gl_Position = osg_Vertex;
}
)GLSL";

constexpr const char INFINITE_FLOOR_FRAGMENT_SHADER[] = R"GLSL(
#version 330 core

uniform mat4 osg_ProjectionMatrix;
uniform mat4 osg_ViewMatrix;
uniform mat4 osg_ViewMatrixInverse;

in vec3 nearPoint;
in vec3 farPoint;

out vec4 fragColor;

float gridLine(vec2 position, float interval) {
	vec2 coord = position / interval;
	vec2 deriv = max(fwidth(coord), vec2(1e-6));
	vec2 distanceToLine = abs(fract(coord - 0.5) - 0.5) / deriv;

	return 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
}

void main() {
	vec3 ray = farPoint - nearPoint;

	if(abs(ray.z) < 1e-6) discard;

	float t = -nearPoint.z / ray.z;

	if(t <= 0.0 || t > 1.0) discard;

	vec3 hit = nearPoint + ray * t;
	vec4 clip = osg_ProjectionMatrix * osg_ViewMatrix * vec4(hit, 1.0);
	float ndcDepth = clip.z / clip.w;

	if(ndcDepth < -1.0 || ndcDepth > 1.0) discard;

	gl_FragDepth = ndcDepth * 0.5 + 0.5;

	float minor = gridLine(hit.xy, 1.0);
	float major = gridLine(hit.xy, 10.0);
	vec3 camera = osg_ViewMatrixInverse[3].xyz / osg_ViewMatrixInverse[3].w;
	float distanceFade = 1.0 - smoothstep(35.0, 180.0, length(hit - camera));
	float horizonFade = smoothstep(0.015, 0.12, abs(normalize(ray).z));
	float fade = distanceFade * horizonFade;
	vec3 color = mix(vec3(0.11, 0.17, 0.25), vec3(0.42, 0.62, 0.88), major);
	float alpha = max(minor * 0.32, major * 0.80) * fade;

	fragColor = vec4(color, alpha);
}
)GLSL";

osg::ref_ptr<osg::Geometry> makeFullscreenTriangle() {
	auto vertices = osgx::make_ref<osg::Vec3Array>();

	vertices->push_back(osg::Vec3(-1.0f, -1.0f, 0.0f));
	vertices->push_back(osg::Vec3(3.0f, -1.0f, 0.0f));
	vertices->push_back(osg::Vec3(-1.0f, 3.0f, 0.0f));

	auto triangle = osgx::make_ref<osg::Geometry>();

	triangle->setVertexArray(vertices);
	triangle->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, 3));

	return triangle;
}

osg::ref_ptr<osg::Node> makeHemisphereSky() {
	auto program = osgx::make_ref<osg::Program>();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, SKY_VERTEX_SHADER));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SKY_FRAGMENT_SHADER));

	auto geode = osgx::make_ref<osg::Geode>();

	geode->setCullingActive(false);
	geode->addDrawable(makeFullscreenTriangle());

	auto* stateSet = geode->getOrCreateStateSet();

	stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
	stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	stateSet->setRenderBinDetails(-10, "RenderBin");

	return geode;
}

osg::ref_ptr<osg::Node> makeInfiniteFloor() {
	auto program = osgx::make_ref<osg::Program>();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, INFINITE_FLOOR_VERTEX_SHADER));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, INFINITE_FLOOR_FRAGMENT_SHADER));

	auto floor = makeFullscreenTriangle();
	auto* stateSet = floor->getOrCreateStateSet();

	stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
	stateSet->setAttributeAndModes(
		new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
		osg::StateAttribute::ON
	);
	stateSet->setAttributeAndModes(
		new osg::Depth(osg::Depth::LESS, 0.0, 1.0, true),
		osg::StateAttribute::ON
	);
	stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
	stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	auto geode = osgx::make_ref<osg::Geode>();

	geode->setCullingActive(false);
	geode->addDrawable(floor);

	return geode;
}

osg::ref_ptr<osg::Node> makePlaceholders() {
	auto geode = osgx::make_ref<osg::Geode>();

	auto addBox = [&](const osg::Vec3& center, const osg::Vec3& size, const osg::Vec4& color) {
		auto box = osgx::make_ref<osg::ShapeDrawable>(new osg::Box(center, size.x(), size.y(), size.z()));

		box->setColor(color);
		geode->addDrawable(box);
	};

	addBox(
		osg::Vec3(-2.0f, 0.5f, 1.0f),
		osg::Vec3(2.0f, 2.0f, 2.0f),
		osg::Vec4(0.88f, 0.32f, 0.24f, 1.0f)
	);
	addBox(
		osg::Vec3(1.5f, 1.5f, 1.5f),
		osg::Vec3(1.5f, 1.5f, 3.0f),
		osg::Vec4(0.22f, 0.65f, 0.94f, 1.0f)
	);
	addBox(
		osg::Vec3(3.8f, -1.2f, 0.6f),
		osg::Vec3(1.2f, 1.2f, 1.2f),
		osg::Vec4(0.95f, 0.76f, 0.22f, 1.0f)
	);

	return geode;
}

class OrbitCaptureBridge: public osgGA::GUIEventHandler {
public:
	OrbitCaptureBridge(osgx::platform::PointerCapture* capture, osgx::OrbitAxisManipulator* manipulator):
		_capture(capture),
		_manipulator(manipulator) {}

	bool handle(const osgGA::GUIEventAdapter& event, osgGA::GUIActionAdapter&) override {
		auto* capture = _capture.get();
		auto* manipulator = _manipulator.get();

		if(!capture || !manipulator) return false;

		if(event.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && event.getKey() == 'c') {
			const bool captured = !capture->isCaptured();

			capture->setCaptured(captured);
			manipulator->setLiveOrbitEnabled(!captured);
			std::cout << "PointerCapture: " << (captured ? "ON" : "OFF") << std::endl;

			return true;
		}

		if(event.getEventType() != osgGA::GUIEventAdapter::FRAME || !capture->isCaptured()) return false;

		const osg::Vec2 delta = capture->consume();
		const double width = event.getXmax() - event.getXmin();
		const double height = event.getYmax() - event.getYmin();
		double dy = delta.y();

		if(event.getMouseYOrientation() == osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS) dy = -dy;
		if(width > 0.0 && height > 0.0) {
			manipulator->orbitByDelta(2.0 * delta.x() / width, 2.0 * dy / height);
		}

		return false;
	}

private:
	osg::observer_ptr<osgx::platform::PointerCapture> _capture;
	osg::observer_ptr<osgx::OrbitAxisManipulator> _manipulator;
};

}

int main(int argc, char** argv) {
	const bool floor = argc > 1 && std::string(argv[1]) == "floor";

	if(argc > 1 && !floor) {
		std::cerr << "Usage: " << argv[0] << " [floor]" << std::endl;

		return 1;
	}

	auto root = osgx::make_ref<osg::Group>();
	auto subject = makePlaceholders();

	root->addChild(makeHemisphereSky());
	root->addChild(subject);
	if(floor) root->addChild(makeInfiniteFloor());

	auto manipulator = osgx::make_ref<osgx::OrbitAxisManipulator>();

	manipulator->setUpAxis(osg::Vec3d(0.0, 0.0, 1.0));
	manipulator->setHomeDirection(osg::Vec3d(0.0, -1.0, 0.0));
	manipulator->setYawSensitivity(osg::PI);
	manipulator->setHeightSensitivity(0.5);
	manipulator->setWheelZoomFactor(1.15);
	manipulator->setCoverageLimits(0.95, 2.0);
	osgViewer::Viewer viewer;

	viewer.setSceneData(root);
	viewer.getCamera()->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
	viewer.setCameraManipulator(manipulator);
	manipulator->setNode(subject);
	viewer.home();
	// The placeholders rest on the z=0 stage. Apply this after home() establishes the subject
	// axis and height, so it cannot disturb viewer/manipulator initialization.
	manipulator->setHeightLimits(0.25, 4.0);

	auto capture = osgx::make_ref<osgx::platform::PointerCapture>(viewer);

	viewer.addEventHandler(capture.get());
	viewer.addEventHandler(new OrbitCaptureBridge(capture.get(), manipulator.get()));

	std::cout
		<< "OrbitAxisManipulator" << std::endl
		<< " Mouse move/drag orbit (X) + height (Y), always active" << std::endl
		<< " Scroll dolly zoom" << std::endl
		<< " Space/Home reset view" << std::endl
		<< " 'c' toggle PointerCapture (unbounded orbit/height)" << std::endl
	;

	return viewer.run();
}
