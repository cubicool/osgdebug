// vimrun! ./examples/osgdebug-imgui

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/ShapeDrawable>
#include <osg/Geode>
#include <osg/MatrixTransform>

#include <osgGA/TrackballManipulator>
#include <osgGA/StateSetManipulator>

OSGX_ENABLE_WARNINGS

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	// Build a small scene: three named spheres at different X positions.
	auto root = osgx::make_nref<osg::Group>("Root");

	// Each sphere gets a progressively higher tessellation detail ratio so they
	// have genuinely different GPU costs - a ground-truth check for the profiler.
	static constexpr float detailRatios[] = { 0.5f, 8.0f, 16.0f };

	for(int i = 0; i < 3; i++) {
		auto xform = osgx::make_nref<osg::MatrixTransform>(
			"Transform_" + std::to_string(i),
			osg::Matrix::translate(osg::Vec3(static_cast<float>(i) * 12.0f, 0.0f, 0.0f))
		);

		auto geode = osgx::make_nref<osg::Geode>("Geode_" + std::to_string(i));
		auto hints = osgx::make_ref<osg::TessellationHints>();

		hints->setDetailRatio(detailRatios[i]);

		auto sphere = osgx::make_ref<osg::ShapeDrawable>(
			new osg::Sphere(osg::Vec3(), 5.0f),
			hints
		);

		sphere->setName("Sphere_x" + std::to_string(static_cast<int>(detailRatios[i])));
		geode->addDrawable(sphere);
		xform->addChild(geode);
		root->addChild(xform);
	}

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));

	// Widget constructor enforces SingleThreaded and pushes itself to the front
	// of the viewer's event handler list automatically.
	auto* gui = new osgDebug::imgui::Widget(viewer);

	gui->addStatsSection();
	gui->addProfilerSection(root);

	// Optional: append app-specific sections below the built-in ones.
	gui->addSection("My App", [](osg::RenderInfo&) {
		ImGui::Text("hello from user app");
	});

	return viewer.run();
}
