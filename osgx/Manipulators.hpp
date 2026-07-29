#pragma once

#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/CullSettings>
#include <osg/Math>
#include <osg/Matrix>
#include <osg/observer_ptr>
#include <osgGA/CameraManipulator>
#include <osgGA/GUIActionAdapter>
#include <osgGA/GUIEventAdapter>

OSGX_ENABLE_WARNINGS

#include <cmath>
#include <vector>

namespace osgx {

// ================================================================================================
// MultiCameraManipulator
//
// Composite camera manipulator that routes input to one active manipulator while letting targets
// drive either the viewer's main camera or a dedicated camera such as an RTT camera.
// ================================================================================================
class MultiCameraManipulator: public osgGA::CameraManipulator {
public:
	struct Target {
		std::string name;
		osg::ref_ptr<osgGA::CameraManipulator> manipulator;
		osg::observer_ptr<osg::Camera> camera;
		osg::observer_ptr<osg::Node> scene;
		std::function<void(bool)> setActive;
	};

	void setToggleKey(int key) { _toggleKey = key; }
	int getToggleKey() const { return _toggleKey; }

	void addTarget(
		const std::string& name,
		osgGA::CameraManipulator* manipulator,
		osg::Camera* camera=nullptr,
		osg::Node* scene=nullptr,
		std::function<void(bool)> setActive={}
	);

	unsigned int getActiveIndex() const { return _active; }
	unsigned int getNumTargets() const { return static_cast<unsigned int>(_targets.size()); }

	Target* activeTarget() {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	const Target* activeTarget() const {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	void activate(unsigned int index);

	void next() {
		if(!_targets.empty()) activate((_active + 1u) % static_cast<unsigned int>(_targets.size()));
	}

	void setByMatrix(const osg::Matrixd& matrix) override;
	void setByInverseMatrix(const osg::Matrixd& matrix) override;
	osg::Matrixd getMatrix() const override;
	osg::Matrixd getInverseMatrix() const override;
	void updateCamera(osg::Camera& mainCamera) override;
	void setNode(osg::Node* node) override;
	osg::Node* getNode() override;
	const osg::Node* getNode() const override;
	void home(double currentTime) override;
	void home(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;
	void init(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;
	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;

private:
	void _updateCamera(osg::Camera& mainCamera);

	std::vector<Target> _targets;
	osg::observer_ptr<osg::Node> _defaultScene;
	unsigned int _active = 0;
	int _toggleKey = 'x';
	bool _hasActive = false;
	bool _mainCameraUpdated = false;
};

// ================================================================================================
// Ortho2DManipulator
//
// Pan/zoom camera manipulator for orthographic 2D scenes.
//
// Controls:
//
// Left drag pan in world XY
// Scroll geometric zoom (_wheelZoomFactor per click)
// Shift+Scroll pixel-nudge zoom (_pixelNudge screen pixels per click)
// Ctrl+Left drag 3D pitch/yaw (unlocks rotation around center)
// Space / Home reset to home
//
// The manipulator owns the projection matrix: updateCamera() sets both view and
// projection each frame, so callers do NOT need to configure the camera projection
// separately.
//
// TODO: consider delegating to or toggling a full OrbitManipulator for persistent
// 3D navigation (currently Ctrl+drag accumulates rotation, release keeps it).
// ================================================================================================
class Ortho2DManipulator: public osgGA::CameraManipulator {
public:
	OSGX_META_Object(osgx, Ortho2DManipulator)

	Ortho2DManipulator() = default;

	OSGX_DISABLE_WARNINGS

		Ortho2DManipulator(
			const Ortho2DManipulator& m,
			const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY
		):
		osgGA::CameraManipulator(m, co),
		_center(m._center),
		_halfExtentY(m._halfExtentY),
		_minHalfExtent(m._minHalfExtent),
		_maxHalfExtent(m._maxHalfExtent),
		_pixelNudge(m._pixelNudge),
		_wheelZoomFactor(m._wheelZoomFactor),
		_rotateSensitivity(m._rotateSensitivity),
		_rotation(m._rotation),
		_node(m._node) {}

	OSGX_ENABLE_WARNINGS

	// Config
	void setPixelNudge(double n) { _pixelNudge = n; }
	double getPixelNudge() const { return _pixelNudge; }

	void setWheelZoomFactor(double f) { _wheelZoomFactor = f; }
	double getWheelZoomFactor() const { return _wheelZoomFactor; }

	void setZoomLimits(double minH, double maxH) { _minHalfExtent = minH; _maxHalfExtent = maxH; }
	void setRotateSensitivity(double s) { _rotateSensitivity = s; }

	// State
	void setCenter(const osg::Vec3d& c) { _center = c; }
	const osg::Vec3d& getCenter() const { return _center; }

	void setHalfExtentY(double h) {
		_halfExtentY = std::clamp(h, _minHalfExtent, _maxHalfExtent);
	}

	double getHalfExtentY() const { return _halfExtentY; }

	// CameraManipulator interface
	void setNode(osg::Node* node) override { _node = node; }
	const osg::Node* getNode() const override { return _node.get(); }
	osg::Node* getNode() override { return _node.get(); }

	// Extract pan center from the translation component of the camera-to-world matrix.
	void setByMatrix(const osg::Matrixd& m) override;
	void setByInverseMatrix(const osg::Matrixd& m) override;

	// Camera-to-world: undo the view matrix composition.
	osg::Matrixd getMatrix() const override;

	// World-to-camera (view matrix).
	// Orbit convention: center to origin -> rotate (pivot is now at origin) -> pull back.
	// OSG uses row vectors, so A*B*C applies A first; pull-back must come last.
	osg::Matrixd getInverseMatrix() const override;

	// Sets BOTH view and projection so the caller owns neither.
	//
	// We take ownership of near/far (DO_NOT_COMPUTE_NEAR_FAR) because OSG's bounding-volume
	// computation clamps near > 0 even for ortho, which clips geometry that lands at negative
	// depth when the camera is tilted in 3D. We derive tight near/far analytically from the
	// scene bounding sphere each frame instead.
	void updateCamera(osg::Camera& cam) override;

	void home(const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter& aa) override;
	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;

private:
	osg::Vec3d _center{0.0, 0.0, 0.0};

	double _halfExtentY{1.0};
	double _minHalfExtent{1e-4};
	double _maxHalfExtent{1e6};
	double _pixelNudge{1.0};
	double _wheelZoomFactor{1.15};
	double _rotateSensitivity{2.0};

	osg::Quat _rotation; // identity = pure top-down 2D
	osg::ref_ptr<osg::Node> _node;

	bool _dragging{false};
	double _lastX{0.0};
	double _lastY{0.0};
};

// ================================================================================================
// OrbitAxisManipulator
//
// "Turntable" camera manipulator for model-viewer-style presentation (see: the Batman Arkham
// series' character/suit viewer). The camera orbits a fixed vertical guide line through the
// model's bounds, always looking level (never pitching up/down) at whatever height it's currently
// at, and dollies toward/away from that line on zoom. The model itself never moves or scales.
//
// Controls:
//
// Mouse move/drag (no button required) orbit (X) + height (Y), always active
// Scroll dolly zoom, clamped by viewport-coverage fraction (see below)
// Space / Home reset to home
//
// State is cylindrical: yaw around the guide line, height along it (clamped to the bound's
// vertical extent -- never below "ground", never above the top), and distance from it (clamped
// so the model can't be zoomed past ~50% visible or zoomed out past a ~5% viewport margin, in
// terms of the camera's current vertical FOV -- see updateCamera()).
//
// The manipulator does NOT own the projection matrix (unlike Ortho2DManipulator) -- it reads the
// camera's existing perspective FOV each frame to recompute the distance clamp, but leaves
// near/far/FOV to the caller.
//
// v1 tracks raw mouse position deltas (bounded by the window edges, like a trackpad) rather than
// true relative/captured motion -- no cursor hide or pointer warp/confine. That's real OS-specific
// plumbing (X11 XGrabPointer/XWarpPointer and friends); see TODO.md for the planned move of the
// existing pyosg/linux platform helpers into osgx before adding it here.
//
// TODO: add optional "gate action X behind button Y" modes (e.g. require LEFT_MOUSE_BUTTON held
// for orbit/height) once the always-active feel is validated -- deliberately left out for now.
// ================================================================================================
class OrbitAxisManipulator: public osgGA::CameraManipulator {
public:
	OSGX_META_Object(osgx, OrbitAxisManipulator)

	OrbitAxisManipulator() = default;

	OSGX_DISABLE_WARNINGS

		OrbitAxisManipulator(
			const OrbitAxisManipulator& m,
			const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY
		):
		osgGA::CameraManipulator(m, co),
		_axis(m._axis),
		_groundY(m._groundY),
		_topY(m._topY),
		_height(m._height),
		_yaw(m._yaw),
		_distance(m._distance),
		_minDistance(m._minDistance),
		_maxDistance(m._maxDistance),
		_minCoverage(m._minCoverage),
		_maxCoverage(m._maxCoverage),
		_yawSensitivity(m._yawSensitivity),
		_heightSensitivity(m._heightSensitivity),
		_wheelZoomFactor(m._wheelZoomFactor),
		_node(m._node) {}

	OSGX_ENABLE_WARNINGS

	// Config
	void setYawSensitivity(double s) { _yawSensitivity = s; }
	void setHeightSensitivity(double s) { _heightSensitivity = s; }
	void setWheelZoomFactor(double f) { _wheelZoomFactor = f; }

	// minCoverage/maxCoverage are fractions of the viewport's vertical extent that the model's
	// bound should occupy at the zoomed-out/zoomed-in extremes, respectively (e.g. 0.95 = 5%
	// margin top/bottom when zoomed out; 2.0 = model is 2x viewport height, ~50% visible, when
	// zoomed in).
	void setCoverageLimits(double minCoverage, double maxCoverage) {
		_minCoverage = minCoverage;
		_maxCoverage = maxCoverage;
	}

	// State
	double getYaw() const { return _yaw; }
	double getHeight() const { return _height; }
	double getDistance() const { return _distance; }

	// CameraManipulator interface
	void setNode(osg::Node* node) override { _node = node; }
	const osg::Node* getNode() const override { return _node.get(); }
	osg::Node* getNode() override { return _node.get(); }

	void setByMatrix(const osg::Matrixd& m) override;
	void setByInverseMatrix(const osg::Matrixd& m) override;
	osg::Matrixd getMatrix() const override;
	osg::Matrixd getInverseMatrix() const override;

	// Leaves the projection matrix untouched; only recomputes the distance clamp (from the
	// camera's current vertical FOV and the model's bound) and sets the view matrix.
	void updateCamera(osg::Camera& cam) override;

	void home(const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter& aa) override;
	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;

private:
	void _orbit(double nx, double ny);

	osg::Vec3d _axis{0.0, 0.0, 0.0}; // guide line: x/z fixed, y spans [_groundY, _topY]
	double _groundY{0.0};
	double _topY{1.0};
	double _height{0.5};
	double _yaw{0.0};
	double _distance{1.0};
	double _minDistance{1e-4};
	double _maxDistance{1e6};
	double _minCoverage{0.95};
	double _maxCoverage{2.0};
	double _yawSensitivity{osg::PI};
	double _heightSensitivity{0.5};
	double _wheelZoomFactor{1.15};

	osg::ref_ptr<osg::Node> _node;

	bool _initialized{false};
	double _lastX{0.0};
	double _lastY{0.0};
};

}
