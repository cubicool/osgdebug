#include "osgx/CameraIntents.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Math>
#include <osgAnimation/EaseMotion>

OSGX_ENABLE_WARNINGS

#include <algorithm>

namespace osgx {

float defaultEase(float t) {
	float result = 0.0f;

	osgAnimation::InOutCubicFunction::getValueAt(t, result);

	return result;
}

FlyToCallback::FlyToCallback(
	const Viewpoint& target,
	double duration,
	std::function<float(float)> ease
):
_target(target), _duration(duration), _ease(std::move(ease)) {
	osg::Matrixd targetCameraToWorld = osg::Matrixd::inverse(
		osg::Matrixd::lookAt(target.eye, target.center, target.up)
	);

	_targetOrientation = targetCameraToWorld.getRotate();
}

bool FlyToCallback::run(osg::Object* object, osg::Object* data) {
	auto* camera = dynamic_cast<osg::Camera*>(data);
	auto* host = dynamic_cast<CameraIntentHost*>(object);

	if(!camera || !host) return false;

	double now = host->currentTime();

	if(_startTime < 0.0) {
		osg::Matrixd startCameraToWorld = osg::Matrixd::inverse(camera->getViewMatrix());

		_startEye = startCameraToWorld.getTrans();
		_startOrientation = startCameraToWorld.getRotate();
		_startTime = now;
	}

	double elapsed = now - _startTime;
	double t = _duration > 0.0 ? std::clamp(elapsed / _duration, 0.0, 1.0) : 1.0;
	double eased = static_cast<double>(_ease(static_cast<float>(t)));

	if(t >= 1.0) {
		osg::Matrixd targetView = osg::Matrixd::lookAt(_target.eye, _target.center, _target.up);

		camera->setViewMatrix(targetView);

		// Resync the manipulator's own state so control hands back to it seamlessly. `this` IS the
		// real manipulator via CameraManipulator<Base>'s inheritance -- no separate "inner" object.
		// See the KNOWN LIMITATION comment in CameraIntents.hpp: this can't perfectly restore an
		// orbit-style manipulator's pivot/distance from a single matrix.
		if(auto* manip = dynamic_cast<osgGA::CameraManipulator*>(object)) {
			manip->setByMatrix(osg::Matrixd::inverse(targetView));
		}

		return false;
	}

	osg::Vec3d eye = _startEye + (_target.eye - _startEye) * eased;
	osg::Quat orientation;

	orientation.slerp(eased, _startOrientation, _targetOrientation);

	osg::Matrixd cameraToWorld = osg::Matrixd::rotate(orientation) * osg::Matrixd::translate(eye);

	camera->setViewMatrix(osg::Matrixd::inverse(cameraToWorld));

	return true;
}

ShakeCallback::ShakeCallback(double intensity, double duration):
_intensity(intensity), _duration(duration) {}

bool ShakeCallback::run(osg::Object* object, osg::Object* data) {
	auto* camera = dynamic_cast<osg::Camera*>(data);
	auto* host = dynamic_cast<CameraIntentHost*>(object);

	if(!camera || !host) return false;

	double now = host->currentTime();

	if(_startTime < 0.0) _startTime = now;

	double elapsed = now - _startTime;

	if(_duration <= 0.0 || elapsed >= _duration) return false;

	double decay = 1.0 - (elapsed / _duration);
	double angleDeg = _intensity * decay;

	osg::Quat jitter(
		osg::DegreesToRadians(angleDeg * _jitter(_rng)), osg::Vec3d(1.0, 0.0, 0.0),
		osg::DegreesToRadians(angleDeg * _jitter(_rng)), osg::Vec3d(0.0, 1.0, 0.0),
		osg::DegreesToRadians(angleDeg * _jitter(_rng)), osg::Vec3d(0.0, 0.0, 1.0)
	);

	camera->setViewMatrix(camera->getViewMatrix() * osg::Matrixd::rotate(jitter));

	return true;
}

}
