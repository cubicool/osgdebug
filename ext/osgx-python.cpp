//vimrun! ./test.py

#include "osgx/osgx.hpp"
#include "osgx/Debug.hpp"
#include "osgx/ImGui.hpp"
#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/functional.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace pyx = pybind11x;

PYBIND11_MODULE(osgx, m) {
	auto py_osg = py::module_::import("OpenSceneGraph");

	osgx::pbr::registerShaderLibs();
	osgx::ibl::registerShaderLibs();

	auto grid = py::class_<
		osgx::Grid,
		osg::Geometry,
		osg::ref_ptr<osgx::Grid>
	>(m, "Grid");

	py::enum_<osgx::Grid::EdgeMode>(grid, "EdgeMode")
		.value("EDGE_ASIS", osgx::Grid::EDGE_ASIS)
		.value("EDGE_HIDE", osgx::Grid::EDGE_HIDE)
		.value("EDGE_NUDGE", osgx::Grid::EDGE_NUDGE)
		.export_values()
	;

	py::enum_<osgx::Grid::LineMode>(grid, "LineMode")
		.value("LINE_SCREEN_PIXELS", osgx::Grid::LINE_SCREEN_PIXELS)
		.value("LINE_GRID_UNITS", osgx::Grid::LINE_GRID_UNITS)
		.export_values()
	;

	grid
		.def(py::init<>())
		.def(py::init<const osg::Vec3&, const osg::Vec3&, const osg::Vec3&>(),
			"corner"_a, "width_vec"_a, "height_vec"_a
		)
		.def_property("canvasSize", &osgx::Grid::getCanvasSize, &osgx::Grid::setCanvasSize)
		.def_property("gridInterval", &osgx::Grid::getGridInterval, &osgx::Grid::setGridInterval)
		.def_property(
			"gridIntervalStrong",
			&osgx::Grid::getGridIntervalStrong,
			&osgx::Grid::setGridIntervalStrong
		)
		.def_property("lineWidthPx", &osgx::Grid::getLineWidthPx, &osgx::Grid::setLineWidthPx)
		.def_property("lineWidth", &osgx::Grid::getLineWidth, &osgx::Grid::setLineWidth)
		.def_property("edgeMode", &osgx::Grid::getEdgeMode, &osgx::Grid::setEdgeMode)
		.def_property("lineMode", &osgx::Grid::getLineMode, &osgx::Grid::setLineMode)
		.def_property("colorBg", &osgx::Grid::getColorBg, &osgx::Grid::setColorBg)
		.def_property("colorLine", &osgx::Grid::getColorLine, &osgx::Grid::setColorLine)
		.def_property(
			"colorLineStrong",
			&osgx::Grid::getColorLineStrong,
			&osgx::Grid::setColorLineStrong
		)
		.def("orthoCamera", &osgx::Grid::orthoCamera)
		.def_static("createOrthoCamera", py::overload_cast<>(&osgx::Grid::createOrthoCamera))
		.def_static(
			"createOrthoCamera",
			py::overload_cast<const osg::Vec3&, const osg::Vec3&, const osg::Vec3&>(
				&osgx::Grid::createOrthoCamera
			),
			"corner"_a, "width_vec"_a, "height_vec"_a
		)
	;

	// osgx::pbr / osgx::ibl - ported from the STATIC path of pyosg-lighting/09-ibl.py and
	// already proven in osgSlug's osgslug-pbr-ibl.cpp; the goal is for Python demos to reuse
	// this toolkit (GLSL snippets + resolveShaderLibs() + the cubemap/BRDF-LUT/SH9 host-side
	// helpers) instead of re-deriving the shader/UBO plumbing from scratch each time.
	m.def(
		"resolveShaderLibs",
		&osgx::resolveShaderLibs,
		"src"_a,
		"Expand '#pragma osgx::pbr ...' / '#pragma osgx::ibl ...' lines into their GLSL source."
	);

	auto m_pbr = m.def_submodule("pbr", "osgx::pbr - BRDF math GLSL snippets + direct-light rig");

	m_pbr.attr("D_GGX") = osgx::pbr::D_GGX;
	m_pbr.attr("G_SCHLICK") = osgx::pbr::G_SCHLICK;
	m_pbr.attr("G_SMITH") = osgx::pbr::G_SMITH;
	m_pbr.attr("F_SCHLICK") = osgx::pbr::F_SCHLICK;
	m_pbr.attr("F_SCHLICK_ROUGHNESS") = osgx::pbr::F_SCHLICK_ROUGHNESS;
	m_pbr.attr("DIRECT_SPECULAR") = osgx::pbr::DIRECT_SPECULAR;
	m_pbr.attr("F_MULTISCATTER") = osgx::pbr::F_MULTISCATTER;
	m_pbr.attr("IBL_SPECULAR") = osgx::pbr::IBL_SPECULAR;
	m_pbr.attr("TONEMAP_PBR_NEUTRAL") = osgx::pbr::TONEMAP_PBR_NEUTRAL;

	m_pbr.def("snippets", &osgx::pbr::snippets);

	py::class_<osgx::pbr::OrbitLightRig::Orbit>(m_pbr, "Orbit")
		.def(
			py::init([](float radius, float height, float speed, float phase, float intensity) {
				return osgx::pbr::OrbitLightRig::Orbit{radius, height, speed, phase, intensity};
			}),
			"radius"_a=0.5f,
			"height"_a=0.5f,
			"speed"_a=0.5f,
			"phase"_a=0.0f,
			"intensity"_a=1.0f
		)
		.def_readwrite("radius", &osgx::pbr::OrbitLightRig::Orbit::radius)
		.def_readwrite("height", &osgx::pbr::OrbitLightRig::Orbit::height)
		.def_readwrite("speed", &osgx::pbr::OrbitLightRig::Orbit::speed)
		.def_readwrite("phase", &osgx::pbr::OrbitLightRig::Orbit::phase)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::Orbit::intensity)
	;

	py::class_<
		osgx::pbr::OrbitLightRig,
		osg::NodeCallback,
		osg::ref_ptr<osgx::pbr::OrbitLightRig>
	>(m_pbr, "OrbitLightRig")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::pbr::OrbitLightRig::ss)
		.def_readwrite("center", &osgx::pbr::OrbitLightRig::center)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::intensity)
		.def_readwrite("uniformName", &osgx::pbr::OrbitLightRig::uniformName)
		.def_readwrite("orbits", &osgx::pbr::OrbitLightRig::orbits)
	;

	auto m_ibl = m.def_submodule(
		"ibl",
		"osgx::ibl - prefiltered cubemap + BRDF LUT + SH9/baked-Lambertian diffuse"
	);

	m_ibl.attr("FULLSCREEN_VERT") = osgx::ibl::FULLSCREEN_VERT;
	m_ibl.attr("BRDF_LUT_FRAG") = osgx::ibl::BRDF_LUT_FRAG;
	m_ibl.attr("SH_IRRADIANCE") = osgx::ibl::SH_IRRADIANCE;
	m_ibl.attr("LAMBERTIAN_IRRADIANCE") = osgx::ibl::LAMBERTIAN_IRRADIANCE;

	py::class_<
		osgx::ibl::RunOnceCallback,
		osg::NodeCallback,
		osg::ref_ptr<osgx::ibl::RunOnceCallback>
	>(m_ibl, "RunOnceCallback")
		.def(py::init<>())
		.def("rebake", &osgx::ibl::RunOnceCallback::rebake, "node"_a)
	;

	m_ibl
		.def(
			"loadPrefilterCubemap",
			&osgx::ibl::loadPrefilterCubemap,
			"path"_a,
			"Loads a pre-baked GGX-prefiltered cubemap (.ktx2); returns None (and logs OSG_WARN) "
			"if the path doesn't load as a TextureCubeMap."
		)
		.def(
			"makeBRDFLUTCamera",
			&osgx::ibl::makeBRDFLUTCamera,
			"lutSize"_a,
			"lut"_a,
			"Configures `lut` in-place (size/format/filters) and returns a PRE_RENDER camera that "
			"bakes the split-sum BRDF LUT into it exactly once (see RunOnceCallback)."
		)
	;

	py::class_<osgx::ibl::SH9>(m_ibl, "SH9")
		.def(py::init<>())
		.def("__len__", [](const osgx::ibl::SH9&) { return 9; })
		.def("__getitem__", [](const osgx::ibl::SH9& self, size_t i) {
			if(i >= 9) throw py::index_error();

			return self.coeffs[i];
		})
		.def("__setitem__", [](osgx::ibl::SH9& self, size_t i, const osg::Vec3f& v) {
			if(i >= 9) throw py::index_error();

			self.coeffs[i] = v;
		})
	;

	m_ibl.def(
		"computeSH",
		&osgx::ibl::computeSH,
		"image"_a,
		"Projects an equirectangular HDR/LDR osg.Image onto SH9 diffuse irradiance coefficients."
	);

	m_ibl.def(
		"computeLambertianCubeMap",
		&osgx::ibl::computeLambertianCubeMap,
		"image"_a,
		"size"_a = 64,
		"samples"_a = 256,
		py::call_guard<py::gil_scoped_release>(),
		"Bakes a cosine-weighted Monte Carlo diffuse irradiance cubemap from an equirectangular "
		"HDR/LDR osg.Image -- more accurate than SH9 (see computeSH), at the cost of a real bake "
		"instead of 9 coefficients. Sample with LAMBERTIAN_IRRADIANCE's osgx_LambertianIrradiance()."
	);

	py::class_<osgx::ibl::GGXPrefilterOptions>(m_ibl, "GGXPrefilterOptions")
		.def(py::init<>())
		.def_readwrite("prefilterSize", &osgx::ibl::GGXPrefilterOptions::prefilterSize)
		.def_readwrite("maxFrames", &osgx::ibl::GGXPrefilterOptions::maxFrames)
		.def_readwrite("readbackFrame", &osgx::ibl::GGXPrefilterOptions::readbackFrame)
		.def_readwrite("syncReadback", &osgx::ibl::GGXPrefilterOptions::syncReadback)
	;

	py::class_<
		osgx::ibl::GGXPrefilterReadback,
		osg::Camera::DrawCallback,
		osg::ref_ptr<osgx::ibl::GGXPrefilterReadback>
	>(m_ibl, "GGXPrefilterReadback")
		.def("isDone", &osgx::ibl::GGXPrefilterReadback::isDone)
		.def("getResult", &osgx::ibl::GGXPrefilterReadback::getResult)
		.def("reset", &osgx::ibl::GGXPrefilterReadback::reset)
	;

	py::class_<osgx::ibl::GGXPrefilterScene>(m_ibl, "GGXPrefilterScene")
		.def_readonly("root", &osgx::ibl::GGXPrefilterScene::root)
		.def_readonly("readback", &osgx::ibl::GGXPrefilterScene::readback)
	;

	m_ibl
		.def(
			"createGGXPrefilterScene",
			&osgx::ibl::createGGXPrefilterScene,
			"equirectImage"_a,
			"options"_a = osgx::ibl::GGXPrefilterOptions()
		)
		.def(
			"rebakeGGXPrefilterScene",
			&osgx::ibl::rebakeGGXPrefilterScene,
			"scene"_a,
			"equirectImage"_a
		)
		.def("finishGGXPrefilter", &osgx::ibl::finishGGXPrefilter, "readback"_a)
	;

	// osgx::debug -- GL_KHR_debug integration (push/pop debug groups, message
	// inserts) plus the two-phase per-drawable GPU/CPU profiler.
	auto m_debug = m.def_submodule("debug", "osgx::debug - GL_KHR_debug integration + GPU/CPU profiler");

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

	// osgx::imgui -- deliberately NOT a general ImGui wrapper (that's pyimgui's
	// job elsewhere). Just enough to build quick debugging knobs inside a
	// Widget::addSection() callback: a handful of stateless functions returning
	// (changed, value) tuples, since Python floats/bools aren't mutable
	// references the way ImGui's C++ &value out-params expect. A sibling of
	// osgx::debug, not nested under it -- most of this (Panel/Widget/sliders) has
	// nothing to do with the profiler; only ProfilerSection reaches into debug::.
#ifdef OSGX_IMGUI
	auto m_imgui = m.def_submodule("imgui", "osgx::imgui namespace");

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
#endif

	py::dict info;

	info["version"] = py::make_tuple(
		OSGX_VERSION_MAJOR,
		OSGX_VERSION_MINOR,
		OSGX_VERSION_PATCH
	);

	pyx::build_info(m, info);
}
