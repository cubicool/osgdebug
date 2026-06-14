// vimrun! ./examples/osgdebug-manipulator
//
// Demonstrates osgx::Ortho2DManipulator.
//
// With no arguments, renders a grid of colored boxes in the XY plane.
// Pass a model path to load and inspect it instead.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <iostream>

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

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	osg::ref_ptr<osg::Node> scene;

	if(argc >= 2) {
		scene = osgDB::readRefNodeFile(argv[1]);

		if(!scene) {
			std::cerr << "Failed to load: " << argv[1] << std::endl;

			return 1;
		}
	}

	else scene = createDefaultScene();

	auto manip = osgx::make_ref<osgx::Ortho2DManipulator>();

	// manip->setPixelNudge(0.5);

	viewer.setSceneData(scene);
	viewer.setCameraManipulator(manip);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	std::cout
		<< "Ortho2DManipulator" << std::endl
		<< " Left drag pan" << std::endl
		<< " Scroll geometric zoom" << std::endl
		<< " Shift+Scroll pixel-nudge zoom (8px/click)" << std::endl
		<< " Ctrl+Left drag 3D pitch/yaw" << std::endl
		<< " Space/Home reset view" << std::endl
	;

	return viewer.run();
}
