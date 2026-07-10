// vimrun! ./examples/osgdebug-grid

#include "../osgx.hpp"

int main(int argc, char** argv) {
	bool orthoMode = argc > 1 && std::string(argv[1]) == "ortho";

	osg::ref_ptr<osg::Node> root;

	if(orthoMode) {
		// Fullscreen NDC quad, always drawn first via a PRE_RENDER camera. Configure the Grid
		// itself before wrapping it, since createOrthoCamera() would only hand back the camera.
		auto grid = osgx::make_ref<osgx::Grid>();

		grid->setCanvasSize(osg::Vec2(600.0f, 600.0f));
		grid->setGridInterval(10.0f);
		grid->setGridIntervalStrong(100.0f);
		grid->setLineWidthPx(1.0f);
		grid->setEdgeMode(osgx::Grid::EDGE_NUDGE);

		root = grid->orthoCamera();
	}

	else {
		// XZ plane (Y-up ground plane), navigable by the default trackball manipulator.
		auto grid = osgx::make_ref<osgx::Grid>(
			osg::Vec3(-1, 0, -1),
			osg::Vec3(2, 0, 0),
			osg::Vec3(0, 0, 2)
		);

		grid->setCanvasSize(osg::Vec2(600.0f, 600.0f));
		grid->setGridInterval(10.0f);
		grid->setGridIntervalStrong(100.0f);
		grid->setLineMode(osgx::Grid::LINE_GRID_UNITS);
		grid->setLineWidth(0.75f);
		// grid->setLineWidth(2.0f);
		grid->setEdgeMode(osgx::Grid::EDGE_ASIS);

		auto geode = osgx::make_ref<osg::Geode>();

		geode->addDrawable(grid);

		root = geode;
	}

	osgViewer::Viewer viewer;

	viewer.setSceneData(root);

	// The ortho camera's own clear IS the frame's first paint; stop the viewer's main
	// camera from immediately stomping it with a second COLOR_BUFFER_BIT clear.
	if(orthoMode) viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT);

	auto r = viewer.run();

	return r;
}
