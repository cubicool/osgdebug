//vimrun! ./test.py

#include "osgDebug.hpp"

#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/functional.h>

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
			[](osgDebug::Source source, osgDebug::Type type, osgDebug::Severity severity, bool enabled) {
				osgDebug::messageControl(source, type, severity, enabled);
			},
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
			py::arg("source") = osgDebug::Source::APPLICATION,
			py::arg("measureTime") = false
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

	py::class_<
		osgDebug::imgui::Widget,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgDebug::imgui::Widget>
	>(m_imgui, "Widget")
		.def(
			py::init<osgViewer::Viewer&, osg::Camera*>(),
			"viewer"_a,
			"draw_camera"_a = nullptr
		)
		.def("addSection", &osgDebug::imgui::Widget::addSection, "label"_a, "fn"_a)
		.def("addStatsSection", &osgDebug::imgui::Widget::addStatsSection)
		.def("addProfilerSection", &osgDebug::imgui::Widget::addProfilerSection, "sceneRoot"_a)
		.def(
			"addTextureSection",
			py::overload_cast<osg::Node*>(&osgDebug::imgui::Widget::addTextureSection),
			"root"_a
		)
		.def(
			"addTextureSection",
			py::overload_cast<>(&osgDebug::imgui::Widget::addTextureSection)
		)
	;

	m_imgui
		.def(
			"slider_float",
			[](const std::string& label, float value, float min, float max, const char* format) {
				bool changed = ImGui::SliderFloat(label.c_str(), &value, min, max, format);

				return py::make_tuple(changed, value);
			},
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"format"_a = "%.3f"
		)
		.def("text", [](const std::string& s) { ImGui::TextUnformatted(s.c_str()); }, "text"_a)
		.def("separator", []() { ImGui::Separator(); })
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
