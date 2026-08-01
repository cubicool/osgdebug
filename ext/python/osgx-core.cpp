#include "osgx-python.hpp"

namespace osgx_python {

void bind_core(py::module_& m) {
	m.def(
		"findDataFile",
		[](
			const std::string& filename,
			const std::vector<std::string>& candidates,
			const std::string& suffix
		) {
			const std::span<const std::string> patterns(candidates);
			const auto result = suffix.empty() ?
				osgx::findDataFile(filename, patterns) :
				osgx::findDataFile(filename, patterns, suffix)
			;

			return result.empty() ? std::string{} : result.string();
		},
		"filename"_a,
		"candidates"_a=std::vector<std::string>{},
		"suffix"_a="",
		"Find filename through OSG_FILE_PATH, optionally trying each {} substitution candidate. "
		"Returns an empty string when no file is found."
	);

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
			"planeNormal",
			&osgx::Ortho2DManipulator::getPlaneNormal,
			&osgx::Ortho2DManipulator::setPlaneNormal,
			"Normal of the unrotated 2D plane (default: +Z)."
		)
		.def_property(
			"screenUp",
			&osgx::Ortho2DManipulator::getScreenUp,
			&osgx::Ortho2DManipulator::setScreenUp,
			"Up direction of the unrotated 2D plane (default: +Y)."
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
			"Inverts the pointer Y axis used for axial motion, in both the raw MOVE/DRAG path and orbitByDelta()."
		)
		.def_property(
			"upAxis",
			&osgx::OrbitAxisManipulator::getUpAxis,
			&osgx::OrbitAxisManipulator::setUpAxis,
			"Turntable up axis (default: +Z)."
		)
		.def_property(
			"homeDirection",
			&osgx::OrbitAxisManipulator::getHomeDirection,
			&osgx::OrbitAxisManipulator::setHomeDirection,
			"Horizontal camera direction at yaw == 0 (default: -Y)."
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
}

}
