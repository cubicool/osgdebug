// vimrun! ./examples/osgdebug-imgui

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/ShapeDrawable>
#include <osg/Geode>
#include <osg/MatrixTransform>

#include <osgGA/TrackballManipulator>
#include <osgGA/StateSetManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	// ImGui requires single-threaded draw -- OSG must not hand the GL context to a separate thread
	// while our pre/post callbacks are installed.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);

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

	// Phase 1: walk the scene and wrap every Drawable with a ProfilerCallback.
	osgDebug::ProfilerVisitor<> profilerVisitor;
	root->accept(profilerVisitor);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));

	// Phase 2: drain GPU timestamp queries after the whole camera finishes rendering.
	// printEvery is set to a large number so the console stays quiet - the ImGui
	// panel is the display surface for this data.
	auto* finalCb = new osgDebug::ProfilerFinalCallback<>(0);

	osgDebug::appendCameraDrawCallback(
		viewer.getCamera(),
		osgDebug::CameraDrawCallbackSlot::FINAL_DRAW,
		finalCb
	);

	auto* gui = new osgDebug::ImGuiHandler();

	gui->onDraw = [finalCb](osg::RenderInfo& ri) {
		const auto contextID = ri.getState()->getContextID();
		const auto& stats = osgDebug::profilerStats(contextID);

		// Cache GL strings on first call - they never change and glGetString
		// is only valid with an active context (guaranteed inside a draw callback).
		static const auto* glRenderer = glGetString(GL_RENDERER);
		static const auto* glVersion = glGetString(GL_VERSION);
		static const auto* glVendor = glGetString(GL_VENDOR);

		ImGui::SetNextWindowSize(ImVec2(600, 320), ImGuiCond_FirstUseEver);

		if(ImGui::Begin("osgDebug Profiler")) {
			// GPU info - always useful, especially when sharing screenshots.
			if(glVendor) ImGui::TextDisabled("Vendor: %s", glVendor);
			if(glRenderer) ImGui::TextDisabled("Renderer: %s", glRenderer);
			if(glVersion) ImGui::TextDisabled("GL: %s", glVersion);

			// TODO: Show VSync / power-throttle state. Prefer a platform-neutral
			// source (lmsensors, sysfs) over driver-specific APIs so we don't
			// bake in NVIDIA assumptions.
			ImGui::Separator();

			auto* fs = ri.getView()->getFrameStamp();

			ImGui::Text("Frame: %u", fs->getFrameNumber());
			ImGui::Text("Time: %.3f s", fs->getSimulationTime());
			ImGui::Separator();

			// QueryMode toggle
			int modeIdx = finalCb->getMode() == osgDebug::QueryMode::SYNC ? 0 : 1;

			if(ImGui::RadioButton("SYNC", &modeIdx, 0)) finalCb->setMode(osgDebug::QueryMode::SYNC);
			ImGui::SameLine();
			if(ImGui::RadioButton("ASYNC", &modeIdx, 1)) finalCb->setMode(osgDebug::QueryMode::ASYNC);
			ImGui::Separator();

			// First pass: sum all drawable averages for the percentage denominator.
			GLuint64 totalNs = 0;

			for(const auto& [path, ps] : stats) totalNs += ps.gpuBuffer.average();

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if(ImGui::BeginTable("gpu_stats", 3, tableFlags)) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Drawable Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("GPU avg (us)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("% of profiled", ImGuiTableColumnFlags_WidthFixed, 160.0f);
				ImGui::TableHeadersRow();

				for(const auto& [path, ps] : stats) {
					const GLuint64 avgNs = ps.gpuBuffer.average();
					const double avgUs = static_cast<double>(avgNs) / 1000.0;
					const float fraction = totalNs > 0
						? static_cast<float>(avgNs) / static_cast<float>(totalNs)
						: 0.0f
					;

					char pctLabel[16];
					std::snprintf(pctLabel, sizeof(pctLabel), "%.1f%%", fraction * 100.0f);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(path.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%.2f", avgUs);
					ImGui::TableSetColumnIndex(2);
					ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), pctLabel);
				}

				ImGui::EndTable();
			}
		}

		ImGui::End();
	};

	// Push to front so ImGui sees events before OSG's camera manipulator.
	viewer.getEventHandlers().push_front(gui);

	return viewer.run();
}
