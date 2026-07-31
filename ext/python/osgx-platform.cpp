#include "osgx-python.hpp"

#ifdef OSGX_EGL
#include "osgx/GraphicsWindowEGL.hpp"
#endif

#ifdef OSGX_GBM
#include "osgx/GraphicsWindowGBM.hpp"
#endif

using namespace std::string_literals;

namespace osgx_python {

// osgx::platform -- X11/XRandr window helpers (alwaysOnTop, listMonitors, moveWindow) plus, when
// built (see OSGX_WITH_EGL/OSGX_WITH_GBM in CMakeLists.txt), the EGL- and GBM/DRM-backed
// GraphicsWindow factories. Moved here from OSG.py's pyosg/linux -- was never actually
// OSG.py-specific, so osgx is the right home; see osgx/Linux.hpp.
void bind_platform(py::module_& m_platform) {
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
}

}
