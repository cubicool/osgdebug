#include "osgx-python.hpp"

namespace osgx_python {

void bind_debug(py::module_& m_debug) {
	py::class_<
		osgx::debug::GraphicsOperation,
		osg::GraphicsOperation,
		osg::ref_ptr<osgx::debug::GraphicsOperation>
	>(m_debug, "GraphicsOperation")
		.def(py::init<>())
	;

	py::enum_<osgx::debug::Severity>(m_debug, "Severity")
		.value("DONT_CARE", osgx::debug::Severity::DONT_CARE)
		.value("HIGH", osgx::debug::Severity::HIGH)
		.value("MEDIUM", osgx::debug::Severity::MEDIUM)
		.value("LOW", osgx::debug::Severity::LOW)
		.value("NOTIFICATION", osgx::debug::Severity::NOTIFICATION)
		.export_values()
	;

	py::enum_<osgx::debug::Source>(m_debug, "Source")
		.value("DONT_CARE", osgx::debug::Source::DONT_CARE)
		.value("API", osgx::debug::Source::API)
		.value("WINDOW_SYSTEM", osgx::debug::Source::WINDOW_SYSTEM)
		.value("SHADER_COMPILER", osgx::debug::Source::SHADER_COMPILER)
		.value("THIRD_PARTY", osgx::debug::Source::THIRD_PARTY)
		.value("APPLICATION", osgx::debug::Source::APPLICATION)
		.value("OTHER", osgx::debug::Source::OTHER)
		.export_values()
	;

	py::enum_<osgx::debug::Type>(m_debug, "Type")
		.value("DONT_CARE", osgx::debug::Type::DONT_CARE)
		.value("ERROR", osgx::debug::Type::ERROR)
		.value("DEPRECATED_BEHAVIOR", osgx::debug::Type::DEPRECATED_BEHAVIOR)
		.value("UNDEFINED_BEHAVIOR", osgx::debug::Type::UNDEFINED_BEHAVIOR)
		.value("PORTABILITY", osgx::debug::Type::PORTABILITY)
		.value("PERFORMANCE", osgx::debug::Type::PERFORMANCE)
		.value("OTHER", osgx::debug::Type::OTHER)
		.value("MARKER", osgx::debug::Type::MARKER)
		.value("PUSH_GROUP", osgx::debug::Type::PUSH_GROUP)
		.value("POP_GROUP", osgx::debug::Type::POP_GROUP)
		.export_values()
	;

	m_debug
		.def("initialize", &osgx::debug::initialize)
		.def(
			"deinitialize",
			&osgx::debug::deinitialize,
			"disableOutput"_a=true
		)
		.def(
			"installDefaultCallback",
			&osgx::debug::installDefaultCallback,
			"synchronous"_a=true
		)
		.def("clearCallback", &osgx::debug::clearCallback)
		.def(
			"enableDebugOutput",
			&osgx::debug::enableDebugOutput,
			"synchronous"_a=true
		)
		.def("disableDebugOutput", &osgx::debug::disableDebugOutput)
		.def(
			"messageControl",
			py::overload_cast<osgx::debug::Source, osgx::debug::Type, osgx::debug::Severity, bool>(
				&osgx::debug::messageControl
			),
			"source"_a,
			"type"_a,
			"severity"_a,
			"enabled"_a
		)
		.def("messageInsert", py::overload_cast<
			osgx::debug::Source,
			osgx::debug::Type,
			GLuint,
			osgx::debug::Severity,
			const std::string&
		>(&osgx::debug::messageInsert),
			"source"_a,
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def("messageInsert", py::overload_cast<
			osgx::debug::Type,
			GLuint,
			osgx::debug::Severity,
			const std::string&
		>(&osgx::debug::messageInsert),
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def(
			"pushGroup",
			py::overload_cast<osgx::debug::Source, GLuint, const std::string&>(&osgx::debug::pushGroup)
		)
		.def(
			"pushGroup",
			py::overload_cast<GLuint, const std::string&>(&osgx::debug::pushGroup)
		)
		.def("popGroup", &osgx::debug::popGroup)
	;

	py::class_<osgx::debug::Scoped>(m_debug, "Scoped")
		.def(
			py::init<GLuint, std::string_view, osgx::debug::Source, bool>(),
			py::arg("id"),
			py::arg("message"),
			py::arg("source")=osgx::debug::Source::APPLICATION,
			py::arg("measureTime")=false
		)
		.def("__enter__", [](osgx::debug::Scoped& self) -> osgx::debug::Scoped& {
			return self;
		})
		.def("__exit__", [](
			osgx::debug::Scoped& self,
			py::object exc_type,
			py::object exc_value,
			py::object traceback
		) {
			return false; // don't suppress Python exceptions
		})
	;
}

}
