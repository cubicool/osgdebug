// vimrun! ./examples/osgdebug-culling

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osgDB/WriteFile>

OSGX_ENABLE_WARNINGS

class CullCallback: public osg::NodeCallback {
public:
	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		std::cout << "CullCallback: " << node->getName() << std::endl;

		traverse(node, nv);
	}
};

class DrawableCullCallback: public osg::DrawableCullCallback {
public:
	virtual bool cull(osg::NodeVisitor*, osg::Drawable* drawable, osg::State* state) const {
		std::cout << "DrawableCullCallback: " << drawable->getName() << std::endl;

		return true;
	}
};

auto createSphereAt(const std::string& name, const osg::Vec3& pos, osgx::vec_t radius) {
	auto m = new osg::MatrixTransform(osg::Matrix::translate(pos));
	auto g = new osg::Geode();
	auto t = new osg::TessellationHints();

	t->setDetailRatio(0.25);

	auto s = new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0, 0.0, 0.0), radius), t);

	s->setName(name + "_Drawable");

	g->setName(name + "_Geode");
	g->addDrawable(s);
	g->setCullCallback(new DrawableCullCallback());

	m->setName(name);
	m->addChild(g);
	m->setCullCallback(new CullCallback());

	return m;
}

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	auto root = osgx::make_ref<osg::Group>();

	osgx::grid(5, 5, [&root](size_t row, size_t col, const osg::Vec3& pos) {
		std::cout << row << "x" << col << " = " << (pos * 10.0) << std::endl;

		std::ostringstream oss;

		oss << "Sphere_" << row << "x" << col;

		root->addChild(createSphereAt(oss.str(), pos * 10.0, 2.5));
	});

	auto dv = osgDebug::DrawVisitor();

	root->accept(dv);

	viewer.setSceneData(root);

	auto r = viewer.run();

	osgDB::writeNodeFile(*root, "osgdebug-culling.osgt");

	return r;
}
