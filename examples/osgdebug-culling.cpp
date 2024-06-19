// vimrun! ./examples/osgdebug-culling

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osgDB/WriteFile>

OSGX_ENABLE_WARNINGS

// TODO: Use this to keep a list of what IS being culled!
class CullCallback: public osg::NodeCallback {
public:
	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		std::cout << "CullCallback: " << node->getName() << " IS VISIBLE!" << std::endl;

		traverse(node, nv);
	}
};

/* class DrawableCullCallback: public osg::DrawableCullCallback {
public:
	virtual bool cull(osg::NodeVisitor*, osg::Drawable* drawable, osg::State* state) const {
		std::cout << "DrawableCullCallback: " << drawable->getName() << std::endl;

		return true;
	}
}; */

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	auto root = osgx::scene::sphereGrid(5, 5);

	osgx::LambdaVisitor<osg::Drawable>([](auto& d) {
		std::cout << &d << ": setting CullCallback on " << d.getName() << std::endl;

		d.setCullCallback(new CullCallback());
	})(root);

	viewer.setSceneData(root);
	viewer.addEventHandler(new osgx::VisitorEventHandler<osgx::DescribeSceneVisitor>(
		osgGA::GUIEventAdapter::KeySymbol::KEY_F9 // 'v'
	));

	auto r = viewer.run();

	osgDB::writeNodeFile(*root, "osgdebug-culling.osgt");

	return r;
}
