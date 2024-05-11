// vimrun! ./examples/osgdebug-culling

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>

#include <osgViewer/Viewer>

#include "../osgx.hpp"

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

	g->setName(name + "_drawable");
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

	for(auto row = 0; row < 5; row++) {
		for(auto col = 0; col < 5; col++) {
			std::ostringstream oss;

			oss << "Sphere" << row << "x" << col;

			root->addChild(createSphereAt(
				oss.str(),
				osg::Vec3(10.0 * col, 0.0, -10.0 * row),
				2.5
			));
		}
	}

	viewer.setSceneData(root);

	auto r = viewer.run();

	// osgDB::writeNodeFile(*root, "tmp.osgt");

	return r;
}
