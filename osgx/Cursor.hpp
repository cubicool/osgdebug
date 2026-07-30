#pragma once

#include "Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Vec2>
#include <osg/observer_ptr>
#include <osgGA/GUIEventHandler>
#include <osgViewer/View>

OSGX_ENABLE_WARNINGS

namespace osgx::platform {

// Shows/hides the OS cursor for the view's current window. No-op if the view has no realized
// GraphicsWindow yet. A plain action, not a get/set pair -- OSG's GraphicsWindow has no visibility
// getter of its own (useCursor()/setCursor() are write-only), and faking one via a shadow value
// would only be tracking osgx's own writes, not the window's real state.
void setCursorVisible(osgViewer::View& view, bool visible=true);

// Warps the OS pointer to (x, y) in view/event coordinates (the same space as
// GUIEventAdapter::getX()/getY(), NOT window-local pixels) without the jump itself being reported
// as motion to whatever next reads a delta against the pre-warp position.
void warpPointer(osgViewer::View& view, float x, float y);

// Software "soft capture" of the mouse: hides the OS cursor and re-centers the pointer every time
// it moves, accumulating the raw motion as a delta instead of exposing absolute screen position --
// the standard hide+warp+accumulate trick for turntable/FPS-style look controls that need
// unbounded relative motion regardless of physical screen size. Motivated by
// osgx::OrbitAxisManipulator (see osgx/Manipulators.hpp), which today maps mouse position directly
// to orbit/height and so can't turn past the physical screen edge -- but this is a general
// primitive, not manipulator-specific; compose it with anything that wants relative-only input.
//
// This is NOT true OS-level pointer confinement: nothing stops the cursor from visibly darting to
// the edge of the screen for one frame between the warp and the next event on some window
// managers/compositors. Real confinement needs XGrabPointer (Linux) and platform equivalents
// elsewhere; not yet implemented -- see TODO.md's "osgx::platform later work".
//
// Deliberately NOT wired into OrbitAxisManipulator itself: Manipulators.hpp is part of the
// always-available osgx.hpp umbrella, while osgx::platform is opt-in (X11/EGL/GBM), so the
// manipulator must not gain a hard dependency on it. Compose the two at the application level
// instead -- add both as event handlers and feed consume()'d deltas into the manipulator.
//
// Usage: add as an ordinary event handler (addEventHandler()), toggle setCaptured() (e.g. on a
// mouse-button press), and poll consume() once per update traversal for the accumulated delta.
class PointerCapture: public osgGA::GUIEventHandler {
public:
	explicit PointerCapture(osgViewer::View& view): _view(&view) {}

	// Hides the cursor and starts warp+accumulate when true; restores the cursor and stops when
	// false. Disabled by default -- callers opt in explicitly.
	void setCaptured(bool captured);
	bool isCaptured() const { return _captured; }

	// Accumulated (dx, dy) since the last call, in view/event coordinate units (the same units as
	// GUIEventAdapter::getX()/getY()). Resets the accumulator to zero so repeated polling -- e.g.
	// once per update traversal -- never double-counts.
	osg::Vec2 consume();

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;

private:
	osg::observer_ptr<osgViewer::View> _view;

	bool _captured = false;
	bool _recenterPending = false;

	float _centerX = 0.0f;
	float _centerY = 0.0f;

	osg::Vec2 _accum{0.0f, 0.0f};
};

}
