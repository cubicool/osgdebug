// vimrun! ./examples/osgx-manipulator
//
// Demonstrates osgx::Ortho2DManipulator (default) and osgx::OrbitAxisManipulator ("orbit").
//
// With no arguments, renders a grid of colored boxes in the XY plane.
// Pass "orbit" to use OrbitAxisManipulator instead of Ortho2DManipulator.
// Pass a model path (after "orbit", if present) to load and inspect it instead of the grid.
//
// In "orbit" mode, press 'c' to toggle osgx::platform::PointerCapture: the cursor hides and
// warps back to window-center every frame, feeding accumulated deltas into
// OrbitAxisManipulator::orbitByDelta() instead of the manipulator's own raw-cursor-position
// tracking (which is bounded by the physical screen edge -- this is the actual motivating test
// for PointerCapture, see TODO.md's "osgx::platform later work"). The manipulator's own
// MOVE/DRAG-driven orbit is disabled for the duration via setLiveOrbitEnabled(false), since OSG
// delivers every event to both the manipulator and every other GUIEventHandler unconditionally
// (see the comment on setLiveOrbitEnabled() in osgx/Manipulators.hpp for why that matters here).

#include "../osgx/Core.hpp"
#include "../osgx/Cursor.hpp"
#include "../osgx/Manipulators.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Geode>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osgDB/ReadFile>
#include <osgGA/GUIEventHandler>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <iostream>
#include <string>

static osg::ref_ptr<osg::Node> createDefaultScene() {
	auto root = osgx::make_ref<osg::Group>();

	struct Entry {
		float x, y;

		osg::Vec4 color;
	};

	static const Entry ENTRIES[] = {
		{ 0.0f, 0.0f, { 1.0f, 0.3f, 0.3f, 1.0f } },
		{ 2.0f, 0.0f, { 0.3f, 1.0f, 0.3f, 1.0f } },
		{ -2.0f, 0.0f, { 0.3f, 0.3f, 1.0f, 1.0f } },
		{ 0.0f, 2.0f, { 1.0f, 1.0f, 0.3f, 1.0f } },
		{ 0.0f, -2.0f, { 0.3f, 1.0f, 1.0f, 1.0f } },
		{ 2.0f, 2.0f, { 1.0f, 0.3f, 1.0f, 1.0f } },
		{ -2.0f, -2.0f, { 0.8f, 0.8f, 0.8f, 1.0f } },
		{ 4.0f, 0.0f, { 1.0f, 0.6f, 0.1f, 1.0f } },
		{ -4.0f, 0.0f, { 0.1f, 0.6f, 1.0f, 1.0f } },
	};

	for(const auto& e : ENTRIES) {
		auto xf = osgx::make_ref<osg::MatrixTransform>(osg::Matrix::translate(e.x, e.y, 0.0f));
		auto geode = osgx::make_ref<osg::Geode>();
		auto box = osgx::make_ref<osg::ShapeDrawable>(new osg::Box(osg::Vec3(), 0.8f, 0.8f, 0.1f));

		box->setColor(e.color);
		geode->addDrawable(box);
		xf->addChild(geode);
		root->addChild(xf);
	}

	return root;
}

// Bridges osgx::platform::PointerCapture into OrbitAxisManipulator::orbitByDelta(): toggles
// capture on 'c', and while captured, feeds each frame's accumulated pixel delta -- normalized by
// the window's half-width/half-height to match orbitByDelta()'s [-1, 1]-ish scale -- into the
// manipulator instead of letting it track the raw cursor itself.
class OrbitCaptureBridge: public osgGA::GUIEventHandler {
public:
	OrbitCaptureBridge(osgx::platform::PointerCapture* capture, osgx::OrbitAxisManipulator* manip):
	_capture(capture), _manip(manip) {}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
		if(ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && ea.getKey() == 'c') {
			auto* capture = _capture.get();
			auto* manip = _manip.get();

			if(!capture || !manip) return false;

			bool captured = !capture->isCaptured();

			capture->setCaptured(captured);
			manip->setLiveOrbitEnabled(!captured);

			std::cout << "PointerCapture: " << (captured ? "ON" : "OFF") << std::endl;

			return true;
		}

		if(ea.getEventType() != osgGA::GUIEventAdapter::FRAME) return false;

		auto* capture = _capture.get();
		auto* manip = _manip.get();

		if(!capture || !manip || !capture->isCaptured()) return false;

		osg::Vec2 delta = capture->consume();
		double w = ea.getXmax() - ea.getXmin();
		double h = ea.getYmax() - ea.getYmin();

		// PointerCapture reports raw ea.getX()/getY() units by design (see osgx/Cursor.hpp) -- it
		// doesn't know or care what those units mean to a caller. orbitByDelta()'s dy, though,
		// matches getYnormalized() (up-positive), the same convention OrbitAxisManipulator's own
		// live MOVE/DRAG tracking uses internally. Raw Y is up-positive or down-positive depending on
		// the window's mouse orientation (X11 defaults to Y_INCREASING_DOWNWARDS), so it must be
		// flipped to match here -- otherwise height responds backwards relative to the uncaptured
		// feel. X has no such flip: getXnormalized() has no orientation dependence.
		double dy = ea.getMouseYOrientation() == osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS
			? -delta.y()
			: delta.y()
		;

		if(w > 0.0 && h > 0.0) manip->orbitByDelta(2.0 * delta.x() / w, 2.0 * dy / h);

		return false;
	}

private:
	osg::observer_ptr<osgx::platform::PointerCapture> _capture;
	osg::observer_ptr<osgx::OrbitAxisManipulator> _manip;
};

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	bool orbitMode = argc >= 2 && std::string(argv[1]) == "orbit";
	const char* modelPath = orbitMode
		? (argc >= 3 ? argv[2] : nullptr)
		: (argc >= 2 ? argv[1] : nullptr)
	;

	osg::ref_ptr<osg::Node> scene;

	if(modelPath) {
		scene = osgDB::readRefNodeFile(modelPath);

		if(!scene) {
			std::cerr << "Failed to load: " << modelPath << std::endl;

			return 1;
		}
	}

	else scene = createDefaultScene();

	viewer.setSceneData(scene);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	if(orbitMode) {
		auto manip = osgx::make_ref<osgx::OrbitAxisManipulator>();

		viewer.setCameraManipulator(manip);

		auto capture = osgx::make_ref<osgx::platform::PointerCapture>(viewer);

		viewer.addEventHandler(capture.get());
		viewer.addEventHandler(new OrbitCaptureBridge(capture.get(), manip.get()));

		std::cout
			<< "OrbitAxisManipulator" << std::endl
			<< " Mouse move/drag orbit (X) + height (Y), always active" << std::endl
			<< " Scroll dolly zoom (clamped to model coverage)" << std::endl
			<< " Space/Home reset view" << std::endl
			<< " 'c' toggle PointerCapture (hide+warp+accumulate; unbounded orbit/height)" << std::endl
		;
	}

	else {
		auto manip = osgx::make_ref<osgx::Ortho2DManipulator>();

		// manip->setPixelNudge(0.5);

		viewer.setCameraManipulator(manip);

		std::cout
			<< "Ortho2DManipulator" << std::endl
			<< " Left drag pan" << std::endl
			<< " Scroll geometric zoom" << std::endl
			<< " Shift+Scroll pixel-nudge zoom (8px/click)" << std::endl
			<< " Ctrl+Left drag 3D pitch/yaw" << std::endl
			<< " Space/Home reset view" << std::endl
		;
	}

	return viewer.run();
}
