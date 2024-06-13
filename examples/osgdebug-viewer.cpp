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
	auto s = new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0, 0.0, 0.0), radius));

	// s->getOrCreateStateSet()->setAttribute(new osg::Point(pSize));
	s->setName("SPHERE");

	return s;
}

auto createSphereAt(const osg::Vec3& pos, osgx::vec_t radius, osgx::vec_t pSize=1.0) {
	auto m = new osg::MatrixTransform(osg::Matrix::translate(pos));
	auto g = new osg::Geode();

	g->addDrawable(createSphere(radius, pSize));

	m->addChild(g);

	return m;
}

int main(int argc, char** argv) {
	osgDebug::FrameByFrameViewer viewer;

	// auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();

	// viewer.setRealizeOperation(debugSupported);
	viewer.realize();

	auto root = osgx::make_ref<osg::Geode>();
	auto draw = createSphere(10.0);

	// draw->setDrawCallback(new osgDebug::DrawCallback());

	root->addDrawable(draw);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());

	osgDebug::pushGroup(0, __FUNCTION__);

	auto r = viewer.run();

	osgDebug::popGroup();

	// osgDB::writeNodeFile(*root, "tmp.osgt");

	return r;
}
