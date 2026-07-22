//vimrun! ./test.py

#include "osgDebug.hpp"

#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <cstring>

namespace py = pybind11;
namespace pyx = pybind11x;

PYBIND11_MODULE(osgDebug, m) {
	auto py_osg = py::module_::import("OpenSceneGraph");

	py::class_<
		osgDebug::GraphicsOperation,
		osg::GraphicsOperation,
		osg::ref_ptr<osgDebug::GraphicsOperation>
	>(m, "GraphicsOperation")
		.def(py::init<>())
	;

	py::enum_<osgDebug::Severity>(m, "Severity")
		.value("DONT_CARE", osgDebug::Severity::DONT_CARE)
		.value("HIGH", osgDebug::Severity::HIGH)
		.value("MEDIUM", osgDebug::Severity::MEDIUM)
		.value("LOW", osgDebug::Severity::LOW)
		.value("NOTIFICATION", osgDebug::Severity::NOTIFICATION)
		.export_values()
	;

	py::enum_<osgDebug::Source>(m, "Source")
		.value("DONT_CARE", osgDebug::Source::DONT_CARE)
		.value("API", osgDebug::Source::API)
		.value("WINDOW_SYSTEM", osgDebug::Source::WINDOW_SYSTEM)
		.value("SHADER_COMPILER", osgDebug::Source::SHADER_COMPILER)
		.value("THIRD_PARTY", osgDebug::Source::THIRD_PARTY)
		.value("APPLICATION", osgDebug::Source::APPLICATION)
		.value("OTHER", osgDebug::Source::OTHER)
		.export_values()
	;

	py::enum_<osgDebug::Type>(m, "Type")
		.value("DONT_CARE", osgDebug::Type::DONT_CARE)
		.value("ERROR", osgDebug::Type::ERROR)
		.value("DEPRECATED_BEHAVIOR", osgDebug::Type::DEPRECATED_BEHAVIOR)
		.value("UNDEFINED_BEHAVIOR", osgDebug::Type::UNDEFINED_BEHAVIOR)
		.value("PORTABILITY", osgDebug::Type::PORTABILITY)
		.value("PERFORMANCE", osgDebug::Type::PERFORMANCE)
		.value("OTHER", osgDebug::Type::OTHER)
		.value("MARKER", osgDebug::Type::MARKER)
		.value("PUSH_GROUP", osgDebug::Type::PUSH_GROUP)
		.value("POP_GROUP", osgDebug::Type::POP_GROUP)
		.export_values()
	;

	m
		.def("initialize", &osgDebug::initialize)
		.def(
			"deinitialize",
			&osgDebug::deinitialize,
			"disableOutput"_a=true
		)
		.def(
			"installDefaultCallback",
			&osgDebug::installDefaultCallback,
			"synchronous"_a=true
		)
		.def("clearCallback", &osgDebug::clearCallback)
		.def(
			"enableDebugOutput",
			&osgDebug::enableDebugOutput,
			"synchronous"_a=true
		)
		.def("disableDebugOutput", &osgDebug::disableDebugOutput)
		.def(
			"messageControl",
			py::overload_cast<osgDebug::Source, osgDebug::Type, osgDebug::Severity, bool>(
				&osgDebug::messageControl
			),
			"source"_a,
			"type"_a,
			"severity"_a,
			"enabled"_a
		)
		.def("messageInsert", py::overload_cast<
			osgDebug::Source,
			osgDebug::Type,
			GLuint,
			osgDebug::Severity,
			const std::string&
		>(&osgDebug::messageInsert),
			"source"_a,
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def("messageInsert", py::overload_cast<
			osgDebug::Type,
			GLuint,
			osgDebug::Severity,
			const std::string&
		>(&osgDebug::messageInsert),
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def(
			"pushGroup",
			py::overload_cast<osgDebug::Source, GLuint, const std::string&>(&osgDebug::pushGroup)
		)
		.def(
			"pushGroup",
			py::overload_cast<GLuint, const std::string&>(&osgDebug::pushGroup)
		)
		.def("popGroup", &osgDebug::popGroup)
	;

	py::class_<osgDebug::Scoped>(m, "Scoped")
		.def(
			py::init<GLuint, std::string_view, osgDebug::Source, bool>(),
			py::arg("id"),
			py::arg("message"),
			py::arg("source")=osgDebug::Source::APPLICATION,
			py::arg("measureTime")=false
		)
		.def("__enter__", [](osgDebug::Scoped& self) -> osgDebug::Scoped& {
			return self;
		})
		.def("__exit__", [](
			osgDebug::Scoped& self,
			py::object exc_type,
			py::object exc_value,
			py::object traceback
		) {
			return false; // don't suppress Python exceptions
		})
	;

	// osgDebug::imgui -- deliberately NOT a general ImGui wrapper (that's pyimgui's
	// job elsewhere). Just enough to build quick debugging knobs inside a
	// Widget::addSection() callback: a handful of stateless functions returning
	// (changed, value) tuples, since Python floats/bools aren't mutable
	// references the way ImGui's C++ &value out-params expect.
#ifdef OSGDEBUG_IMGUI
	auto m_imgui = m.def_submodule("imgui", "osgDebug::imgui namespace");

	py::enum_<osgDebug::imgui::Dock>(m_imgui, "Dock")
		.value("NONE", osgDebug::imgui::Dock::NONE)
		.value("LEFT", osgDebug::imgui::Dock::LEFT)
		.value("RIGHT", osgDebug::imgui::Dock::RIGHT)
		.export_values()
	;

	py::class_<osgDebug::imgui::Options>(m_imgui, "Options")
		.def(py::init<>())
		.def_readwrite("show_gpu_info", &osgDebug::imgui::Options::showGPUInfo)
		.def_readwrite("show_frame_info", &osgDebug::imgui::Options::showFrameInfo)
		.def_readwrite("dock", &osgDebug::imgui::Options::dock)
		.def_readwrite("dock_width", &osgDebug::imgui::Options::dockWidth)
	;

	// A growable options bag for addSection() -- expected to grow (a size hint
	// beyond expand/constrain, tooltips, etc.), so this is keyword-constructible
	// from Python rather than adding more positional args to addSection itself.
	py::class_<osgDebug::imgui::SectionOptions>(m_imgui, "SectionOptions")
		.def(
			py::init(&osgDebug::imgui::makeSectionOptions),
			"expand"_a=false,
			"default_open"_a=false
		)
		.def_readwrite("expand", &osgDebug::imgui::SectionOptions::expand)
		.def_readwrite("default_open", &osgDebug::imgui::SectionOptions::defaultOpen)
	;

	py::class_<osgDebug::imgui::Panel>(m_imgui, "Panel")
		.def(py::init<>())
		.def(
			"addSection", &osgDebug::imgui::Panel::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgDebug::imgui::SectionOptions()
		)
		.def("removeSection", &osgDebug::imgui::Panel::removeSection, "label"_a)
		.def("clearSections", &osgDebug::imgui::Panel::clearSections)
		.def(
			"addStatsSection",
			&osgDebug::imgui::Panel::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgDebug::imgui::Panel::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<
				osgViewer::View&,
				osg::Node*, bool
			>(&osgDebug::imgui::Panel::addTextureSection),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgDebug::imgui::Panel::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
		.def("draw", &osgDebug::imgui::Panel::draw, "render_info"_a)
	;

	py::class_<
		osgDebug::imgui::Widget,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgDebug::imgui::Widget>
	>(m_imgui, "Widget")
		.def(
			// osgViewer::View, not the full Viewer -- Widget only needs getCamera()
			// (cached once) and getEventHandlers(); a Python osgViewer::Viewer still
			// works here since it upcasts (View is separately registered above and
			// Viewer's py::class_ lists it as a base).
			py::init<osgViewer::View&, osg::Camera*, osgDebug::imgui::Options>(),
			"viewer"_a,
			"draw_camera"_a=nullptr,
			"options"_a=osgDebug::imgui::Options()
		)
		.def(
			"addSection",
			&osgDebug::imgui::Widget::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgDebug::imgui::SectionOptions()
		)
		.def("removeSection", &osgDebug::imgui::Widget::removeSection, "label"_a)
		.def("clearSections", &osgDebug::imgui::Widget::clearSections)
		// Widget no longer holds a Viewer/View reference of its own (see the C++
		// class comment), so these now take one explicitly instead of reusing
		// whatever Widget was constructed with.
		.def(
			"addStatsSection",
			&osgDebug::imgui::Widget::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgDebug::imgui::Widget::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, osg::Node*, bool>(
				&osgDebug::imgui::Widget::addTextureSection
			),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgDebug::imgui::Widget::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
	;

	m_imgui
		.def(
			"slider_float",
			&osgDebug::imgui::sliderFloat,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"format"_a="%.3f"
		)
		.def(
			"slider_float_nudge",
			&osgDebug::imgui::sliderFloatNudge,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"step_pct"_a=0.01f,
			"format"_a="%.3f"
		)
		.def("text", &osgDebug::imgui::text, "text"_a)
		.def("separator", &osgDebug::imgui::separator)
		.def(
			"checkbox",
			&osgDebug::imgui::checkbox,
			"label"_a,
			"value"_a
		)
		.def(
			"input_text",
			&osgDebug::imgui::inputText,
			"label"_a,
			"value"_a,
			"max_length"_a=256,
			"enter_returns_true"_a=false
		)
		.def(
			"radio_group",
			&osgDebug::imgui::radioGroup,
			"value"_a,
			"labels"_a,
			"same_line"_a=true
		)
	;
#endif // OSGDEBUG_IMGUI

#if 0
	m.doc() = "osgDebug - OpenSceneGraph + GL_KHR_debug (https://github.com/cubicool/osgDebug)";

	py::dict info;

	info["version"] = py::make_tuple(
		OSGSLUG_VERSION_MAJOR,
		OSGSLUG_VERSION_MINOR,
		OSGSLUG_VERSION_PATCH
	);

	pyx::build_info(m, info);
#endif
}
