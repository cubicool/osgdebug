#pragma once

#include "Core.hpp"
#include "Manipulators.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Callback>
#include <osg/Camera>
#include <osg/Matrix>
#include <osg/Quat>
#include <osg/Vec3d>
#include <osgGA/CameraManipulator>

OSGX_ENABLE_WARNINGS

#include <functional>
#include <random>

namespace osgx {

// ================================================================================================
// Built-in camera intents
//
// Plain osg::Callback subclasses (no dependency on any concrete CameraManipulator<Base>
// instantiation), meant to be attached via CameraManipulator<Base>::addUpdateCameraCallback() (see
// osgx/Manipulators.hpp). Both cast their `object` argument to osgx::CameraIntentHost to read
// currentTime() (FRAME-event time, cached by the mixin -- not a polled osg::Timer), and their
// `data` argument to osg::Camera to read/write the view matrix currently being composed.
// ================================================================================================

// FlyToCallback's `ease` parameter is a plain (float) -> float callable, not a bespoke enum -- pass
// any of osgAnimation::EaseMotion's ~28 curve functions (osgAnimation/EaseMotion; already a linked
// osgx dependency, and the same library OpenSceneGraph.py's `osgAnimation` Python module exposes:
// e.g. `osgAnimation.in_out_cubic`, `.out_bounce`, `.out_elastic`, ...), a Motion instance's
// get_value_at, or a fully custom curve. defaultEase() is what's used when the caller doesn't pass
// one -- delegates to InOutCubicFunction, not a hand-rolled approximation.
float defaultEase(float t);

struct Viewpoint {
	osg::Vec3d eye;
	osg::Vec3d center;
	osg::Vec3d up{0.0, 0.0, 1.0};
};

// One-shot: animates from the camera's current pose to `target` over `duration` seconds (lerping
// eye position, slerping orientation -- NOT lerping two lookAt() "center" points directly, which
// breaks down when start/target orientations are far apart), then resyncs the manipulator's own
// state via CameraManipulator::setByMatrix() so control hands back to it seamlessly.
//
// KNOWN LIMITATION: setByMatrix() cannot perfectly restore an orbit-style manipulator's pivot/
// distance from a single matrix (osgGA::OrbitManipulator::setByMatrix(), the base of
// TrackballManipulator, reconstructs its center using its OWN stale pre-flight distance -- a
// single 4x4 matrix can't encode an orbit pivot). The camera looks correct immediately after this
// completes, but the manipulator's orbit center/distance may be off until something re-homes it --
// the first post-flight drag/zoom on an orbit-style Base may pivot around the wrong point.
//
// Add with runOnce=true -- this callback returns false exactly once, on the frame it completes.
class FlyToCallback: public osg::Callback {
public:
	FlyToCallback(
		const Viewpoint& target,
		double duration,
		std::function<float(float)> ease=defaultEase
	);

	bool run(osg::Object* object, osg::Object* data) override;

private:
	Viewpoint _target;
	double _duration;
	std::function<float(float)> _ease;
	osg::Quat _targetOrientation;

	// Captured lazily on the FIRST run(), not at construction -- avoids reading a stale/zero
	// currentTime() if this is attached before the first real FRAME event of a run.
	double _startTime = -1.0;
	osg::Vec3d _startEye;
	osg::Quat _startOrientation;
};

// Composes decaying rotational jitter (magnitude scaled by 1 - elapsed/duration, `intensity` being
// the maximum jitter angle in degrees) on top of whatever's already in the camera's view matrix by
// the time this runs -- right-multiplied (finalView = baseView * jitterDelta), matching OSG's
// row-vector convention (see Ortho2DManipulator's own comment on multiplication order in
// src/Manipulators.cpp). The manipulator's own internal state is never touched.
//
// Add with runOnce=true if you want it to self-remove once decayed to zero -- runOnce=false would
// leave a spent, inert entry in the callback list forever.
class ShakeCallback: public osg::Callback {
public:
	ShakeCallback(double intensity, double duration);

	bool run(osg::Object* object, osg::Object* data) override;

private:
	double _intensity;
	double _duration;
	double _startTime = -1.0; // same lazy-capture reasoning as FlyToCallback

	std::mt19937 _rng{std::random_device{}()};
	std::uniform_real_distribution<double> _jitter{-1.0, 1.0};
};

}
