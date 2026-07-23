#pragma once

#include "Core.hpp"

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
	) {
		Target target;
		target.name = name;
		target.manipulator = manipulator;
		target.camera = camera;
		target.scene = scene;
		target.setActive = setActive;

		if(target.manipulator.valid()) {
			target.manipulator->setNode(scene ? scene : _defaultScene.get());
		}

		_targets.push_back(target);

		if(_targets.size() == 1) activate(0);
	}

	unsigned int getActiveIndex() const { return _active; }
	unsigned int getNumTargets() const { return static_cast<unsigned int>(_targets.size()); }

	Target* activeTarget() {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	const Target* activeTarget() const {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	void activate(unsigned int index) {
		if(index >= _targets.size()) return;

		if(_hasActive && _active == index) return;

		if(_hasActive && _active < _targets.size() && _targets[_active].setActive) {
			_targets[_active].setActive(false);
		}

		_active = index;
		_hasActive = true;

		auto& target = _targets[_active];

		if(target.manipulator.valid()) {
			target.manipulator->finishAnimation();
			target.manipulator->setNode(target.scene.valid() ? target.scene.get() : _defaultScene.get());

			if(target.camera.valid()) target.manipulator->setByInverseMatrix(target.camera->getViewMatrix());
		}

		if(target.setActive) target.setActive(true);

		OSG_NOTICE << "MultiCameraManipulator: " << target.name << std::endl;
	}

	void next() {
		if(!_targets.empty()) activate((_active + 1u) % static_cast<unsigned int>(_targets.size()));
	}

	void setByMatrix(const osg::Matrixd& matrix) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->setByMatrix(matrix);
		}
	}

	void setByInverseMatrix(const osg::Matrixd& matrix) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->setByInverseMatrix(matrix);
		}
	}

	osg::Matrixd getMatrix() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getMatrix() : osg::Matrixd();
	}

	osg::Matrixd getInverseMatrix() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getInverseMatrix() : osg::Matrixd();
	}

	void updateCamera(osg::Camera& mainCamera) override {
		auto* target = activeTarget();

		if(!target || !target->manipulator.valid()) return;

		_updateCamera(mainCamera);

		if(target->camera.valid()) target->manipulator->updateCamera(*target->camera);
		else target->manipulator->updateCamera(mainCamera);
	}

	void setNode(osg::Node* node) override {
		_defaultScene = node;

		for(auto& target : _targets) {
			if(target.manipulator.valid()) {
				target.manipulator->setNode(target.scene.valid() ? target.scene.get() : _defaultScene.get());
			}
		}
	}

	osg::Node* getNode() override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
	}

	const osg::Node* getNode() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
	}

	void home(double currentTime) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->home(currentTime);
		}
	}

	void home(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->home(ea, aa);
		}
	}

	void init(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->init(ea, aa);
		}
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && ea.getKey() == _toggleKey) {
			if(auto* target = activeTarget(); target && target->manipulator.valid()) {
				target->manipulator->init(ea, aa);
			}

			next();

			if(auto* target = activeTarget(); target && target->manipulator.valid()) {
				target->manipulator->init(ea, aa);
			}

			return true;
		}

		auto* target = activeTarget();

		return target && target->manipulator.valid() && target->manipulator->handle(ea, aa);
	}

private:
	void _updateCamera(osg::Camera& mainCamera) {
		if(_mainCameraUpdated) return;

		_mainCameraUpdated = true;

		for(auto& candidate : _targets) {
			if(!candidate.camera.valid() && candidate.manipulator.valid()) {
				candidate.manipulator->finishAnimation();
				candidate.manipulator->setNode(candidate.scene.valid() ? candidate.scene.get() : _defaultScene.get());
				candidate.manipulator->home(0.0);
				candidate.manipulator->updateCamera(mainCamera);

				break;
			}
		}
	}

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
	void setByMatrix(const osg::Matrixd& m) override {
		_center.set(m(3, 0), m(3, 1), m(3, 2));
	}

	void setByInverseMatrix(const osg::Matrixd& m) override {
		setByMatrix(osg::Matrixd::inverse(m));
	}

	// Camera-to-world: undo the view matrix composition.
	osg::Matrixd getMatrix() const override {
		return osg::Matrixd::inverse(getInverseMatrix());
	}

	// World-to-camera (view matrix).
	// Orbit convention: center to origin -> rotate (pivot is now at origin) -> pull back.
	// OSG uses row vectors, so A*B*C applies A first; pull-back must come last.
	osg::Matrixd getInverseMatrix() const override {
		return
			osg::Matrixd::translate(-_center) *
			osg::Matrixd::rotate(_rotation) *
			osg::Matrixd::translate(0.0, 0.0, -1.0)
		;
	}

	// Sets BOTH view and projection so the caller owns neither.
	//
	// We take ownership of near/far (DO_NOT_COMPUTE_NEAR_FAR) because OSG's bounding-volume
	// computation clamps near > 0 even for ortho, which clips geometry that lands at negative
	// depth when the camera is tilted in 3D. We derive tight near/far analytically from the
	// scene bounding sphere each frame instead.
	void updateCamera(osg::Camera& cam) override {
		cam.setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
		cam.setViewMatrix(getInverseMatrix());

		const auto* vp = cam.getViewport();
		double aspect = (vp && vp->height() > 0.0)
			? vp->width() / vp->height()
			: 1.0
		;

		double h = _halfExtentY;
		double nearPlane = -1e6;
		double farPlane = 1e6;

		if(_node.valid()) {
			osg::BoundingSphere bs = _node->getBound();

			if(bs.radius() > 0.0) {
				// Eye position and forward vector in world space, derived from the view matrix.
				// Orbit: eye = center + rotation.conj() * (0, 0, 1)
				osg::Vec3d eye = _center + _rotation.conj() * osg::Vec3d(0.0, 0.0, 1.0);
				osg::Vec3d fwd = _rotation.conj() * osg::Vec3d(0.0, 0.0, -1.0);

				// Signed depth of the scene center along the view axis.
				double depth = (osg::Vec3d(bs.center()) - eye) * fwd;
				double r = bs.radius() * 1.1; // 10% padding

				nearPlane = depth - r;
				farPlane = depth + r;
			}
		}

		cam.setProjectionMatrixAsOrtho(
			-h * aspect, h * aspect,
			-h, h,
			nearPlane, farPlane
		);
	}

	void home(const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter& aa) override {
		_rotation = osg::Quat();

		if(_node.valid()) {
			auto bs = _node->getBound();

			_center = osg::Vec3d(bs.center());
			_halfExtentY = (bs.radius() > 0.0) ? bs.radius() * 1.2 : 1.0;
		}

		else {
			_center.set(0.0, 0.0, 0.0);
			_halfExtentY = 1.0;
		}

		aa.requestRedraw();
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		switch(ea.getEventType()) {
		case osgGA::GUIEventAdapter::PUSH:
			_lastX = ea.getXnormalized();
			_lastY = ea.getYnormalized();
			_dragging = (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);

			return false;

		case osgGA::GUIEventAdapter::RELEASE:
			_dragging = false;

			return false;

		case osgGA::GUIEventAdapter::DRAG: {
			if(!_dragging) return false;

			double nx = ea.getXnormalized();
			double ny = ea.getYnormalized();
			double dx = nx - _lastX;
			double dy = ny - _lastY;

			_lastX = nx;
			_lastY = ny;

			bool ctrl = (ea.getModKeyMask() & osgGA::GUIEventAdapter::MODKEY_CTRL) != 0;

			if(ctrl) {
				// 3D: pitch/yaw orbit around center.
				// Yaw rotates around world Y; pitch rotates around the camera's current right axis.
				osg::Vec3d right = _rotation.conj() * osg::Vec3d(1.0, 0.0, 0.0);

				_rotation =
					osg::Quat(dy * _rotateSensitivity, right) *
					osg::Quat(-dx * _rotateSensitivity, osg::Vec3d(0.0, 1.0, 0.0)) *
					_rotation
				;
			}

			else {
				// Pan along camera right/up so speed is constant regardless of rotation.
				double aspect = ea.getWindowWidth() > 0
					? double(ea.getWindowWidth()) / double(ea.getWindowHeight())
					: 1.0
				;

				osg::Vec3d right = _rotation.conj() * osg::Vec3d(1.0, 0.0, 0.0);
				osg::Vec3d up = _rotation.conj() * osg::Vec3d(0.0, 1.0, 0.0);

				_center -= right * dx * _halfExtentY * aspect;
				_center -= up * dy * _halfExtentY;
			}

			aa.requestRedraw();

			return false;
		}

		case osgGA::GUIEventAdapter::SCROLL: {
			bool shift = (ea.getModKeyMask() & osgGA::GUIEventAdapter::MODKEY_SHIFT) != 0;
			bool up = (ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP);

			if(shift) {
				// Pixel-nudge: each click moves the visible boundary by exactly _pixelNudge pixels.
				int winH = ea.getWindowHeight();
				double worldPerPixel = 2.0 * _halfExtentY / double(winH > 0 ? winH : 1);

				_halfExtentY += (up ? -1.0 : 1.0) * _pixelNudge * worldPerPixel;
			}

			else _halfExtentY *= up ? (1.0 / _wheelZoomFactor) : _wheelZoomFactor;

			_halfExtentY = std::clamp(_halfExtentY, _minHalfExtent, _maxHalfExtent);

			aa.requestRedraw();

			return true;
		}

		case osgGA::GUIEventAdapter::KEYDOWN:
			if(
				ea.getKey() == osgGA::GUIEventAdapter::KEY_Space ||
				ea.getKey() == osgGA::GUIEventAdapter::KEY_Home
			) {
				home(ea, aa);
				return true;
			}

			return false;

		default:
			return false;
		}
	}

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


}
