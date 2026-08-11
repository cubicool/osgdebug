#pragma once

#include <string>
#include <vector>

namespace osgViewer { class Viewer; }

namespace osgx::platform {

// Pins the viewer's native X11 window above other windows via the EWMH `_NET_WM_STATE`
// ClientMessage protocol every window manager is expected to honor; see:
// https://specifications.freedesktop.org/wm-spec/latest/ar01s05.html#NETWMSTATE
void alwaysOnTop(osgViewer::Viewer& viewer, bool enabled=true);

// A single XRandR monitor, in real (possibly non-adjacent/overlapping) root-window coordinates --
// do not assume a flush left-to-right layout.
struct Monitor {
	std::string name;

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	bool primary = false;
};

// Queries the real XRandR monitor layout (position/size in root-window coordinates). Monitors
// are NOT assumed to be flush/adjacent -- use these rects directly for placement math.
std::vector<Monitor> listMonitors();

// Repositions (and optionally resizes) an already-realized X11 window in one call: moves the real
// X11 window via XMoveResizeWindow, then calls GraphicsContext::resized() so OSG's own viewport/
// camera bookkeeping stays in sync. Pass width/height <= 0 to keep the window's current size.
void moveWindow(osgViewer::Viewer& viewer, int x, int y, int width=-1, int height=-1);

// Retitles the real X11 window (title bar + icon/taskbar name), via
// GraphicsWindowX11::setWindowName() -- osg::GraphicsContext already exposes this as a
// real cross-platform virtual (osgViewer::GraphicsWindow::setWindowName(), implemented
// for X11 via XStoreName/XSetIconName), but GraphicsWindowX11 itself is not registered
// in pyosg's Python bindings (only the methodless GraphicsWindow base is), so pybind11's
// RTTI-based polymorphic dispatch can't reach it from `camera.graphicsContext` -- this
// does the dynamic_cast in C++ instead, the same shape as alwaysOnTop()/moveWindow()
// above, so nothing X11-specific needs to be exposed to Python at all.
void setWindowTitle(osgViewer::Viewer& viewer, const std::string& title);

}
