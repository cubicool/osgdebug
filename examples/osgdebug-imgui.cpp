// vimrun! ./examples/osgdebug-imgui

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/ShapeDrawable>
#include <osg/Geode>

#include <osgGA/TrackballManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	// ImGui requires single-threaded draw -- OSG must not hand the GL context to a separate thread
	// while our pre/post callbacks are installed.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);

	auto root = osgx::make_ref<osg::Geode>();
	auto shape = osgx::make_ref<osg::ShapeDrawable>(new osg::Sphere(osg::Vec3(), 5.0f));

	root->addDrawable(shape);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());

	auto* gui = new osgDebug::ImGuiHandler();

	gui->onDraw = [](osg::RenderInfo& ri) {
		ImGui::SetNextWindowSize(ImVec2(300, 120), ImGuiCond_FirstUseEver);

		if(ImGui::Begin("osgDebug")) {
			auto* fs = ri.getView()->getFrameStamp();

			ImGui::Text("Frame: %u", fs->getFrameNumber());
			ImGui::Text("Time: %.3f s", fs->getSimulationTime());
			ImGui::Separator();
			ImGui::TextDisabled("osgDebug::ImGuiHandler - it works!");
		}

		ImGui::End();
	};

	// Push to front so ImGui sees events before OSG's camera manipulator.
	viewer.getEventHandlers().push_front(gui);

	return viewer.run();
}
