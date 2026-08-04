#include "osgx-python.hpp"

// osgx::imgui -- deliberately NOT a general ImGui wrapper (that's pyimgui's job elsewhere). Just
// enough to build quick debugging knobs inside a Widget::addSection() callback: a handful of
// stateless functions returning (changed, value) tuples, since Python floats/bools aren't
// mutable references the way ImGui's C++ &value out-params expect. A sibling of osgx::debug, not
// nested under it -- most of this (Panel/Widget/sliders) has nothing to do with the profiler;
// only ProfilerSection reaches into debug::.
//
// This whole file only compiles (and is only added to the source list, see CMakeLists.txt) when
// OSGX_WITH_IMGUI is on -- osgx::imgui's own types don't exist otherwise (see osgx/ImGui.hpp).
#ifdef OSGX_IMGUI

namespace osgx_python {

void bind_imgui(py::module_& m_imgui) {
	py::enum_<osgx::imgui::Dock>(m_imgui, "Dock")
		.value("NONE", osgx::imgui::Dock::NONE)
		.value("LEFT", osgx::imgui::Dock::LEFT)
		.value("RIGHT", osgx::imgui::Dock::RIGHT)
		.export_values()
	;

	py::class_<osgx::imgui::Options>(m_imgui, "Options")
		.def(py::init<>())
		.def_readwrite("show_gpu_info", &osgx::imgui::Options::showGPUInfo)
		.def_readwrite("show_frame_info", &osgx::imgui::Options::showFrameInfo)
		.def_readwrite("dock", &osgx::imgui::Options::dock)
		.def_readwrite("dock_width", &osgx::imgui::Options::dockWidth)
	;

	// A growable options bag for addSection() -- expected to grow (a size hint
	// beyond expand/constrain, tooltips, etc.), so this is keyword-constructible
	// from Python rather than adding more positional args to addSection itself.
	py::class_<osgx::imgui::SectionOptions>(m_imgui, "SectionOptions")
		.def(
			py::init(&osgx::imgui::makeSectionOptions),
			"expand"_a=false,
			"default_open"_a=false
		)
		.def_readwrite("expand", &osgx::imgui::SectionOptions::expand)
		.def_readwrite("default_open", &osgx::imgui::SectionOptions::defaultOpen)
	;

	py::class_<osgx::imgui::Panel>(m_imgui, "Panel")
		.def(py::init<>())
		.def(
			"addSection", &osgx::imgui::Panel::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgx::imgui::SectionOptions()
		)
		.def("removeSection", &osgx::imgui::Panel::removeSection, "label"_a)
		.def("clearSections", &osgx::imgui::Panel::clearSections)
		.def(
			"addStatsSection",
			&osgx::imgui::Panel::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgx::imgui::Panel::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false,
			"print_every"_a=0
		)
		.def(
			"addTextureSection",
			py::overload_cast<
				osgViewer::View&,
				osg::Node*, bool
			>(&osgx::imgui::Panel::addTextureSection),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgx::imgui::Panel::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
		.def("draw", &osgx::imgui::Panel::draw, "render_info"_a)
	;

	py::class_<
		osgx::imgui::Widget,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgx::imgui::Widget>
	>(m_imgui, "Widget")
		.def(
			// osgViewer::View, not the full Viewer -- Widget only needs getCamera()
			// (cached once) and getEventHandlers(); a Python osgViewer::Viewer still
			// works here since it upcasts (View is separately registered above and
			// Viewer's py::class_ lists it as a base).
			py::init<osgViewer::View&, osg::Camera*, osgx::imgui::Options>(),
			"viewer"_a,
			"draw_camera"_a=nullptr,
			"options"_a=osgx::imgui::Options()
		)
		.def(
			"addSection",
			&osgx::imgui::Widget::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgx::imgui::SectionOptions()
		)
		.def("removeSection", &osgx::imgui::Widget::removeSection, "label"_a)
		.def("clearSections", &osgx::imgui::Widget::clearSections)
		// Widget no longer holds a Viewer/View reference of its own (see the C++
		// class comment), so these now take one explicitly instead of reusing
		// whatever Widget was constructed with.
		.def(
			"addStatsSection",
			&osgx::imgui::Widget::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgx::imgui::Widget::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false,
			"print_every"_a=0
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, osg::Node*, bool>(
				&osgx::imgui::Widget::addTextureSection
			),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgx::imgui::Widget::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
	;

	m_imgui
		.def(
			"slider_float",
			&osgx::imgui::sliderFloat,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"format"_a="%.3f"
		)
		.def(
			"slider_float_nudge",
			&osgx::imgui::sliderFloatNudge,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"step_pct"_a=0.01f,
			"format"_a="%.3f"
		)
		.def("text", &osgx::imgui::text, "text"_a)
		.def("separator", &osgx::imgui::separator)
		.def(
			"checkbox",
			&osgx::imgui::checkbox,
			"label"_a,
			"value"_a
		)
		.def("button", &osgx::imgui::button, "label"_a)
		.def(
			"color_edit3",
			&osgx::imgui::colorEdit3,
			"label"_a,
			"r"_a,
			"g"_a,
			"b"_a
		)
		.def(
			"input_text",
			&osgx::imgui::inputText,
			"label"_a,
			"value"_a,
			"max_length"_a=256,
			"enter_returns_true"_a=false
		)
		.def(
			"radio_group",
			&osgx::imgui::radioGroup,
			"value"_a,
			"labels"_a,
			"same_line"_a=true
		)
	;
}

}

#endif // OSGX_IMGUI
