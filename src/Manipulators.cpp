#include "osgx/Manipulators.hpp"

namespace osgx {

void MultiCameraManipulator::addTarget(
	const std::string& name,
	osgGA::CameraManipulator* manipulator,
	osg::Camera* camera,
	osg::Node* scene,
	std::function<void(bool)> setActive
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

void MultiCameraManipulator::activate(unsigned int index) {
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

void MultiCameraManipulator::setByMatrix(const osg::Matrixd& matrix) {
	if(auto* target = activeTarget(); target && target->manipulator.valid()) {
		target->manipulator->setByMatrix(matrix);
	}
}

void MultiCameraManipulator::setByInverseMatrix(const osg::Matrixd& matrix) {
	if(auto* target = activeTarget(); target && target->manipulator.valid()) {
		target->manipulator->setByInverseMatrix(matrix);
	}
}

osg::Matrixd MultiCameraManipulator::getMatrix() const {
	auto* target = activeTarget();

	return target && target->manipulator.valid() ? target->manipulator->getMatrix() : osg::Matrixd();
}

osg::Matrixd MultiCameraManipulator::getInverseMatrix() const {
	auto* target = activeTarget();

	return target && target->manipulator.valid() ? target->manipulator->getInverseMatrix() : osg::Matrixd();
}

void MultiCameraManipulator::updateCamera(osg::Camera& mainCamera) {
	auto* target = activeTarget();

	if(!target || !target->manipulator.valid()) return;

	_updateCamera(mainCamera);

	if(target->camera.valid()) target->manipulator->updateCamera(*target->camera);
	else target->manipulator->updateCamera(mainCamera);
}

void MultiCameraManipulator::setNode(osg::Node* node) {
	_defaultScene = node;

	for(auto& target : _targets) {
		if(target.manipulator.valid()) {
			target.manipulator->setNode(target.scene.valid() ? target.scene.get() : _defaultScene.get());
		}
	}
}

osg::Node* MultiCameraManipulator::getNode() {
	auto* target = activeTarget();

	return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
}

const osg::Node* MultiCameraManipulator::getNode() const {
	auto* target = activeTarget();

	return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
}

void MultiCameraManipulator::home(double currentTime) {
	if(auto* target = activeTarget(); target && target->manipulator.valid()) {
		target->manipulator->home(currentTime);
	}
}

void MultiCameraManipulator::home(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
	if(auto* target = activeTarget(); target && target->manipulator.valid()) {
		target->manipulator->home(ea, aa);
	}
}

void MultiCameraManipulator::init(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
	if(auto* target = activeTarget(); target && target->manipulator.valid()) {
		target->manipulator->init(ea, aa);
	}
}

bool MultiCameraManipulator::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
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

void MultiCameraManipulator::_updateCamera(osg::Camera& mainCamera) {
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

// Extract pan center from the translation component of the camera-to-world matrix.
void Ortho2DManipulator::setByMatrix(const osg::Matrixd& m) {
	_center.set(m(3, 0), m(3, 1), m(3, 2));
}

void Ortho2DManipulator::setByInverseMatrix(const osg::Matrixd& m) {
	setByMatrix(osg::Matrixd::inverse(m));
}

osg::Matrixd Ortho2DManipulator::getMatrix() const {
	return osg::Matrixd::inverse(getInverseMatrix());
}

// World-to-camera (view matrix).
// Orbit convention: center to origin -> rotate (pivot is now at origin) -> pull back.
// OSG uses row vectors, so A*B*C applies A first; pull-back must come last.
osg::Matrixd Ortho2DManipulator::getInverseMatrix() const {
	return
		osg::Matrixd::translate(-_center) *
		osg::Matrixd::rotate(_rotation) *
		osg::Matrixd::translate(0.0, 0.0, -1.0)
	;
}

void Ortho2DManipulator::updateCamera(osg::Camera& cam) {
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

void Ortho2DManipulator::home(const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter& aa) {
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

bool Ortho2DManipulator::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
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

}
