// vimrun! ./examples/osgdebug-viewer

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgGA/TrackballManipulator>

#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <vector>
#include <iostream>
#include <sstream>

auto createSphere(osgx::vec_t radius, osgx::vec_t pSize=1.0) {
	auto s = osgx::make_ref<osg::ShapeDrawable>(new osg::Sphere(osg::Vec3(0.0, 0.0, 0.0), radius));

	// s->getOrCreateStateSet()->setAttribute(new osg::Point(pSize));
	s->setName("SPHERE");

	return s;
}

auto createSphereAt(const osg::Vec3& pos, osgx::vec_t radius, osgx::vec_t pSize=1.0) {
	auto m = osgx::make_ref<osg::MatrixTransform>(osg::Matrix::translate(pos));
	auto g = osgx::make_ref<osg::Geode>();

	g->addDrawable(createSphere(radius, pSize));

	m->addChild(g);

	return m;
}

int main(int argc, char** argv) {
	osgDebug::FrameByFrameViewer viewer;

	auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();

	viewer.setRealizeOperation(debugSupported);
	viewer.realize();

	auto root = osgx::make_ref<osg::Node>(nullptr);

	if(argc >= 2) {
		root = osgDB::readRefNodeFile(argv[1]);

		if(!root) return 0;
	}

	else {
		auto geode = osgx::make_ref<osg::Geode>();
		auto draw = createSphere(10.0);

		geode->addDrawable(draw);

		root = geode;
	}

	auto dsv = osgx::DescribeSceneVisitor();
	auto dv = osgDebug::ProfilerVisitor();

	root->accept(dsv);
	root->accept(dv);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	// viewer.addEventHandler(new osgViewer::StatsHandler());

	osgDebug::appendCameraDrawCallback(
		viewer.getCamera(),
		osgDebug::CameraDrawCallbackSlot::FINAL_DRAW,
		new osgDebug::ProfilerFinalCallback()
	);

	osgDebug::pushGroup(0, __FUNCTION__);

	auto r = viewer.run();

	osgDebug::popGroup();

	// osgDB::writeNodeFile(*root, "tmp.osgt");

	return r;
}
