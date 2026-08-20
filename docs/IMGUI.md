# `osgx::imgui` — Dear ImGui overlay

`osgx/ImGui.hpp` (gated behind `OSGX_IMGUI`, requiring Dear ImGui via pkg-config; set via the
`OSGX_WITH_IMGUI` CMake option, which compiles it into `libosgx` when Dear ImGui is found)
provides
`osgx::imgui::Widget`, a self-contained Dear ImGui overlay with pluggable sections
(`addStatsSection()`, `addProfilerSection()`, `addTextureSection()`, or a custom
`addSection(label, fn)`), and `osgx::imgui::Panel`, the same section machinery for an
application that already owns its own ImGui context/frame/window.

`addProfilerSection()` is the [`osgx::debug`](DEBUG.md) front-end — the only place `osgx::imgui`
reaches into that subsystem; everything else (`Panel`, the stats/texture browsers) has nothing to
do with `GL_KHR_debug` specifically.

See `examples/osgx-imgui.cpp` (self-owned context) and `examples/osgx-imgui-external.cpp`
(app-owned context/frame/window pattern) for worked examples.
