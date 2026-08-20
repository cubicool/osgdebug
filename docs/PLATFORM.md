# `osgx::platform` — X11/EGL/GBM window helpers

`osgx/Linux.hpp` provides X11/XRandr window helpers that are always compiled into `libosgx` (X11
is already a hard dependency of `osgViewer`'s GLX backend on Linux, so this carries no more risk
than the existing OpenGL dependency):

- `alwaysOnTop(viewer, enabled=true)` — pins the viewer's X11 window above other windows via the
  EWMH `_NET_WM_STATE` ClientMessage protocol.
- `Monitor` / `listMonitors()` — the real XRandR monitor layout (position/size in root-window
  coordinates; monitors are **not** assumed to be flush/adjacent).
- `moveWindow(viewer, x, y, width=-1, height=-1)` — repositions (and optionally resizes) an
  already-realized X11 window, keeping OSG's own viewport/camera bookkeeping in sync.

`osgx/GraphicsWindowEGL.hpp` and `osgx/GraphicsWindowGBM.hpp` (gated behind `OSGX_EGL`/`OSGX_GBM`,
requiring EGL and GBM+libdrm respectively via pkg-config; set via the `OSGX_WITH_EGL`/
`OSGX_WITH_GBM` CMake options, which auto-detect and compile into `libosgx` when found — same
pattern as `OSGX_WITH_IMGUI`) provide two experimental `osgViewer::GraphicsWindow` factories:

- `createEGLWindow(traits)` — an ordinary X11 window driven by EGL instead of GLX.
  Skeleton/proof-of-concept: no input/resize handling, but does watch `WM_DELETE_WINDOW` so
  closing the window shuts the viewer down cleanly.
- `createGBMWindow(traits)` — direct-scanout DRM/KMS + GBM, no X11 or window manager at all
  (kiosk/embedded-style). Requires exclusive DRM master access, so it fails under a running X
  server or Wayland compositor.

`osgx/Cursor.hpp` provides mouse capture built purely on portable `osgGA`/`osgViewer` virtuals
(`GraphicsWindow::useCursor()`, `View::requestWarpPointer()`) — no X11 dependency of its own, but
grouped under `osgx::platform` since that's where the motivating need already lived:

- `setCursorVisible(view, visible=true)` / `warpPointer(view, x, y)` — small standalone action
  helpers (not a get/set pair — OSG's `GraphicsWindow` has no visibility getter of its own).
- `PointerCapture` — a `GUIEventHandler` implementing the standard hide+warp+accumulate trick for
  turntable/FPS-style relative-motion look controls: while `setCaptured(true)`, hides the cursor
  and re-centers it on every move, accumulating the delta for `consume()` to poll once per update
  traversal. **Not** true OS-level pointer confinement (nothing stops the cursor visibly darting to
  the screen edge for one frame between warps on some window managers) — that needs real
  `XGrabPointer` work, tracked in `TODO.md`.

Migrated from `OpenSceneGraph.py`'s `pyosg/linux/` (it was never actually OSG.py-specific); see
`examples/osgx-platform.cpp` for a worked example of all of the above, including both alternate
window backends.
