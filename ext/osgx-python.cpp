//vimrun! ./test.py

#include "osgx/osgx.hpp"
#include "osgx/Cursor.hpp"
#include "osgx/Debug.hpp"
#include "osgx/ImGui.hpp"
#include "osgx/Linux.hpp"
#include "pyosg/pyosg.hpp"

#ifdef OSGX_EGL
#include "osgx/GraphicsWindowEGL.hpp"
#endif

#ifdef OSGX_GBM
#include "osgx/GraphicsWindowGBM.hpp"
#endif

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

	// osgx::Ortho2DManipulator / OrbitAxisManipulator / MultiCameraManipulator (osgx/Manipulators.hpp).
	// All three derive from osgGA::CameraManipulator, already registered by pyosgGA.cpp as
	// "CameraManipulator" -- its node/matrix/inverseMatrix/homePosition properties and
	// home()/init()/handle() methods come along automatically via virtual dispatch, so only each
	// subclass's OWN new surface needs binding here.
	py::class_<
		osgx::Ortho2DManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::Ortho2DManipulator>
	>(m, "Ortho2DManipulator")
		.def(py::init<>())
		.def_property(
			"pixelNudge",
			&osgx::Ortho2DManipulator::getPixelNudge,
			&osgx::Ortho2DManipulator::setPixelNudge
		)
		.def_property(
			"wheelZoomFactor",
			&osgx::Ortho2DManipulator::getWheelZoomFactor,
			&osgx::Ortho2DManipulator::setWheelZoomFactor
		)
		.def_property(
			"rotateSensitivity",
			&osgx::Ortho2DManipulator::getRotateSensitivity,
			&osgx::Ortho2DManipulator::setRotateSensitivity
		)
		.def_property(
			"invertY",
			&osgx::Ortho2DManipulator::getInvertY,
			&osgx::Ortho2DManipulator::setInvertY,
			"Inverts the Y axis for Ctrl-drag 3D pitch only; plain pan is unaffected."
		)
		.def_property(
			"invertX",
			&osgx::Ortho2DManipulator::getInvertX,
			&osgx::Ortho2DManipulator::setInvertX,
			"Inverts the X axis for Ctrl-drag 3D yaw only; plain pan is unaffected."
		)
		.def_property(
			"center",
			&osgx::Ortho2DManipulator::getCenter,
			&osgx::Ortho2DManipulator::setCenter
		)
		.def_property(
			"halfExtentY",
			&osgx::Ortho2DManipulator::getHalfExtentY,
			&osgx::Ortho2DManipulator::setHalfExtentY
		)
		.def_property(
			"zoomLimits",
			&osgx::Ortho2DManipulator::getZoomLimits,
			[](osgx::Ortho2DManipulator& self, py::object obj) {
				auto vals = pyx::try_unpack_sequence<double, double>(obj);

				if(!vals) throw py::type_error(
					"Expected a (minHalfExtent, maxHalfExtent) sequence of length 2"
				);

				auto& [minH, maxH] = *vals;

				self.setZoomLimits(minH, maxH);
			},
			"(minHalfExtent, maxHalfExtent) clamp for halfExtentY."
		)
	;

	py::class_<
		osgx::OrbitAxisManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::OrbitAxisManipulator>
	>(m, "OrbitAxisManipulator")
		.def(py::init<>())
		.def_property(
			"yawSensitivity",
			&osgx::OrbitAxisManipulator::getYawSensitivity,
			&osgx::OrbitAxisManipulator::setYawSensitivity
		)
		.def_property(
			"heightSensitivity",
			&osgx::OrbitAxisManipulator::getHeightSensitivity,
			&osgx::OrbitAxisManipulator::setHeightSensitivity
		)
		.def_property(
			"wheelZoomFactor",
			&osgx::OrbitAxisManipulator::getWheelZoomFactor,
			&osgx::OrbitAxisManipulator::setWheelZoomFactor
		)
		.def_property(
			"invertY",
			&osgx::OrbitAxisManipulator::getInvertY,
			&osgx::OrbitAxisManipulator::setInvertY,
			"Inverts the Y axis used for height, in both the raw MOVE/DRAG path and orbitByDelta()."
		)
		.def_property(
			"coverageLimits",
			&osgx::OrbitAxisManipulator::getCoverageLimits,
			[](osgx::OrbitAxisManipulator& self, py::object obj) {
				auto vals = pyx::try_unpack_sequence<double, double>(obj);

				if(!vals) throw py::type_error(
					"Expected a (minCoverage, maxCoverage) sequence of length 2"
				);

				auto& [minC, maxC] = *vals;

				self.setCoverageLimits(minC, maxC);
			},
			"(minCoverage, maxCoverage) viewport-coverage fractions at the zoom extremes."
		)
		.def_property(
			"liveOrbitEnabled",
			&osgx::OrbitAxisManipulator::isLiveOrbitEnabled,
			&osgx::OrbitAxisManipulator::setLiveOrbitEnabled,
			"Disable to drive orbit/height exclusively via orbitByDelta() (e.g. from "
			"osgx.platform.PointerCapture) instead of raw MOVE/DRAG cursor tracking."
		)
		.def_property_readonly("yaw", &osgx::OrbitAxisManipulator::getYaw)
		.def_property_readonly("height", &osgx::OrbitAxisManipulator::getHeight)
		.def_property_readonly("distance", &osgx::OrbitAxisManipulator::getDistance)
		.def(
			"orbitByDelta",
			&osgx::OrbitAxisManipulator::orbitByDelta,
			"dx"_a,
			"dy"_a,
			"Applies a pre-computed (dx, dy) directly, in the same normalized units as "
			"GUIEventAdapter.xNormalized/yNormalized."
		)
	;

	py::class_<
		osgx::MultiCameraManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::MultiCameraManipulator>
	>(m, "MultiCameraManipulator")
		.def(py::init<>())
		.def_property(
			"toggleKey",
			&osgx::MultiCameraManipulator::getToggleKey,
			&osgx::MultiCameraManipulator::setToggleKey
		)
		.def_property_readonly("activeIndex", &osgx::MultiCameraManipulator::getActiveIndex)
		.def_property_readonly("numTargets", &osgx::MultiCameraManipulator::getNumTargets)
		.def(
			"addTarget",
			&osgx::MultiCameraManipulator::addTarget,
			"name"_a,
			"manipulator"_a,
			"camera"_a=nullptr,
			"scene"_a=nullptr,
			"setActive"_a=std::function<void(bool)>()
		)
		.def("activate", &osgx::MultiCameraManipulator::activate, "index"_a)
		.def("next", &osgx::MultiCameraManipulator::next)
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
		.def_property_readonly("done", &osgx::ibl::GGXPrefilterReadback::isDone)
		.def_property_readonly("result", &osgx::ibl::GGXPrefilterReadback::getResult)
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

	// osgx::platform -- X11/XRandr window helpers (alwaysOnTop, listMonitors, moveWindow) plus,
	// when built (see OSGX_WITH_EGL/OSGX_WITH_GBM in CMakeLists.txt), the EGL- and GBM/DRM-backed
	// GraphicsWindow factories. Moved here from OSG.py's pyosg/linux -- was never actually
	// OSG.py-specific, so osgx is the right home; see osgx/Linux.hpp.
	auto m_platform = m.def_submodule("platform", "osgx::platform - X11/EGL/GBM window helpers");

	m_platform.def(
		"alwaysOnTop",
		&osgx::platform::alwaysOnTop,
		"viewer"_a,
		"enabled"_a=true,
		"Pin the viewer's native X11 window above other windows (EWMH _NET_WM_STATE_ABOVE)."
	);

	py::class_<osgx::platform::Monitor>(m_platform, "Monitor")
		.def_readonly("name", &osgx::platform::Monitor::name)
		.def_readonly("x", &osgx::platform::Monitor::x)
		.def_readonly("y", &osgx::platform::Monitor::y)
		.def_readonly("width", &osgx::platform::Monitor::width)
		.def_readonly("height", &osgx::platform::Monitor::height)
		.def_readonly("primary", &osgx::platform::Monitor::primary)
		.def("__repr__", [](const osgx::platform::Monitor& self) {
			return
				"Monitor(name='"s + self.name + "', "
				"x="s + std::to_string(self.x) + ", "
				"y="s + std::to_string(self.y) + ", "
				"width="s + std::to_string(self.width) + ", "
				"height="s + std::to_string(self.height) + ", "
				"primary="s + (self.primary ? "True"s : "False"s) + ")"s
			;
		})
	;

	m_platform.def(
		"listMonitors",
		&osgx::platform::listMonitors,
		"Query the real XRandR monitor layout (position/size in root-window coordinates). Monitors "
		"are NOT assumed to be flush/adjacent -- use these rects directly for placement math."
	);

	m_platform.def(
		"moveWindow",
		&osgx::platform::moveWindow,
		"viewer"_a,
		"x"_a,
		"y"_a,
		"width"_a=-1,
		"height"_a=-1,
		"Reposition (and optionally resize) an already-realized X11 window, keeping OSG's own "
		"viewport bookkeeping in sync. Pass width/height <= 0 to keep the current size."
	);

#ifdef OSGX_EGL
	m_platform.def(
		"createEGLWindow",
		&osgx::platform::createEGLWindow,
		"traits"_a,
		"Create an X11 window driven by EGL (instead of GLX). Skeleton/proof-of-concept: assign "
		"the result to `camera.graphicsContext`."
	);
#endif

#ifdef OSGX_GBM
	m_platform.def(
		"createGBMWindow",
		&osgx::platform::createGBMWindow,
		"traits"_a,
		"Create a direct-scanout DRM/KMS+GBM window (no X11, no window manager). Skeleton/proof-"
		"of-concept: requires exclusive DRM master access, so it will fail under a running X "
		"server. Assign the result to `camera.graphicsContext`."
	);
#endif

	m_platform.def(
		"setCursorVisible",
		&osgx::platform::setCursorVisible,
		"view"_a,
		"visible"_a=true,
		"Show/hide the OS cursor for the view's current window."
	);

	m_platform.def(
		"warpPointer",
		&osgx::platform::warpPointer,
		"view"_a,
		"x"_a,
		"y"_a,
		"Warp the OS pointer to (x, y) in view/event coordinates (GUIEventAdapter.x/y space, not "
		"window-local pixels) without the jump itself registering as motion."
	);

	// Software hide+warp+accumulate mouse capture for turntable/FPS-style relative-motion look
	// controls. NOT true OS-level pointer confinement -- see osgx/Cursor.hpp.
	py::class_<
		osgx::platform::PointerCapture,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgx::platform::PointerCapture>
	>(m_platform, "PointerCapture")
		.def(py::init<osgViewer::View&>(), "view"_a)
		.def_property(
			"captured",
			&osgx::platform::PointerCapture::isCaptured,
			&osgx::platform::PointerCapture::setCaptured
		)
		.def("consume", &osgx::platform::PointerCapture::consume)
	;

	py::dict info;

	info["version"] = py::make_tuple(
		OSGX_VERSION_MAJOR,
		OSGX_VERSION_MINOR,
		OSGX_VERSION_PATCH
	);

	pyx::build_info(m, info);
}
