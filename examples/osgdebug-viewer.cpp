// vimrun! ./examples/osgdebug-viewer

#include <osg/GLExtensions>
#include <osg/Group>
#include <osg/Notify>
#include <osg/Geometry>
#include <osg/ArgumentParser>
#include <osg/ApplicationUsage>
#include <osg/Texture2D>
#include <osg/Geode>
#include <osg/PagedLOD>

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>
#include <osg/Point>
#include <osg/PolygonMode>

#include <osgGA/TrackballManipulator>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileNameUtils>

#include <osgViewer/Viewer>

#include <vector>
#include <iostream>
#include <sstream>

#include "../osgx.hpp"
#include "../osgDebug.hpp"

auto createSphere(osgx::vec_t radius, osgx::vec_t pSize=1.0) {
	auto s = new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0, 0.0, 0.0), radius));

	/* s->getOrCreateStateSet()->setAttributeAndModes(
	// s->getOrCreateStateSet()->setAttribute(
		new osg::Point(pSize),
		// osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE
		osg::StateAttribute::ON
	); */

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

	osgDebug::pushGroup(0, __FUNCTION__);

	auto r = viewer.run();

	osgDebug::popGroup();

	// osgDB::writeNodeFile(*root, "tmp.osgt");

	return r;
}
