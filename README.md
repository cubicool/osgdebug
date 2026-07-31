# osgx

osgx is a small, compiled C++20 utility layer on top of OpenSceneGraph. It modernizes common OSG
idioms (concepts, ranges, spans, lambdas) and adds four optional, explicitly-included subsystems:

> [!NOTE]
> The `x` in `osgx` just means "eXtras"; it has nothing to do with `X11` or
> anything else Xorg-centric (`osgx::platform`'s X11/XRandr window helpers notwithstanding).

- `osgx::debug` — `GL_KHR_debug` integration (driver message callback, KHR debug-group annotations)
  plus a two-phase GPU/CPU per-drawable profiler.
- `osgx::imgui` — a Dear ImGui overlay (`Widget`/`Panel`) with pluggable sections and a built-in
  GPU-profiler, OSG-stats, and scene-texture browser.
- `osgx::platform` — X11/XRandr window helpers (`alwaysOnTop`, `listMonitors`, `moveWindow`),
  EGL- and GBM/DRM-backed `GraphicsWindow` factories for driving a window without GLX or X11 at
  all, and `PointerCapture` for hide+warp+accumulate mouse capture (turntable/FPS-style look
  controls).
- `osgx::gltf` — a glTF 2.0 loader (`osgdb_gltf`), plus an optional `osgx::gltf::pbribl` adapter that
  renders it using the generic `osgx::pbr`/`osgx::ibl` facilities. Merged in from the formerly
  separate `osgGLTF` repo.

All four live under `osgx/` and use the `osgx` namespace, but none of them is included by the
`osgx.hpp` umbrella header — pulling in `GL_KHR_debug`, Dear ImGui, X11/EGL/GBM, or the glTF loader
is always an explicit opt-in (`#include "osgx/Debug.hpp"` / `#include "osgx/ImGui.hpp"` / `#include
"osgx/Linux.hpp"` / `#include "osgx/gltf/Reader.hpp"`), never something a consumer gets for free
just by including the umbrella.

# CMake

osgx can be consumed directly from its source tree or as an installed CMake package. In either
case, link the imported-style target and its OpenSceneGraph include and link requirements will be
propagated to your target.

To embed the source tree in another project:

```cmake
add_subdirectory(path/to/osgx EXCLUDE_FROM_ALL)
target_link_libraries(my_target PRIVATE osgx::osgx)
```

When embedded, examples, utilities, the Python module, and installation rules are disabled by
default. They can be enabled individually with `OSGX_BUILD_EXAMPLES`, `OSGX_BUILD_UTILS`,
`OSGX_BUILD_PYTHON`, and `OSGX_INSTALL`.

To install osgx and consume it as a package, first install an already-configured build tree:

```console
cmake --install BUILD --prefix /path/to/prefix
```

Then use the installed package from the consuming project:

```cmake
find_package(osgx CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE osgx::osgx)
```

Pass `-DCMAKE_PREFIX_PATH=/path/to/prefix` when configuring the consuming project if the chosen
prefix is not already in CMake's search path.

The same build tree can remove exactly the files recorded by its most recent install:

```console
cmake --build BUILD --target uninstall
```

This uses CMake's generated `install_manifest.txt`; keep the build tree until the installation is
no longer needed.

Use `osgx::core` for the low-level, header-only facilities (`Core.hpp`, `Array.hpp`,
`Callbacks.hpp`, `Shader.hpp`, `Visitors.hpp`, `Warnings.hpp`) or `osgx::osgx` for the complete
utility layer, including `osgx::debug`/`osgx::imgui`/`osgx::platform`. `osgx::osgx` is the compiled
`libosgx` library and includes `osgx::core`.

`osgx::gltf` (the glTF loader) and `osgx::ktx2` (the KTX2 reader/writer) are separate optional
static libraries, each gated behind its own `OSGX_BUILD_GLTF`/`OSGX_BUILD_KTX2` option (default ON
at the top level, OFF when embedded). They used to live in a separate `osgGLTF` repo; both were
folded directly into this tree since they were never meaningfully independent of `osgx::core`/
`osgx::osgx` to begin with. See `osgx::gltf` below.

# `osgx.hpp` — public headers

`osgx.hpp` is the umbrella header for the always-available utility layer. It keeps common setup
code shorter and adds modern range/span/lambda-friendly wrappers. Its public headers are organized
by concern:

- `osgx/Core.hpp` — smart-pointer helpers, timing, ring buffers, `findDataFile()`, and the
  `ObjectPath`/`vec_t`/literal utilities.
- `osgx/Visitors.hpp` — scene-graph visitors, event handlers, and `FilterNotifyHandler`.
- `osgx/Array.hpp` — `Array<BaseArray>` / `DrawElements<BaseElements>` wrappers.
- `osgx/Callbacks.hpp` — callback-group and lambda-callback adapters.
- `osgx/Picking.hpp` — object-ID picking cameras, readback, and hover/click handlers.
- `osgx/Manipulators.hpp` — `Ortho2DManipulator`, `OrbitAxisManipulator`, and `MultiCameraManipulator`.
- `osgx/Grid.hpp` — procedurally generated, antialiased grid overlay/ground-plane geometry.
- `osgx/Shader.hpp` — generic, line-oriented GLSL library expansion.
- `osgx/PBR.hpp` — PBR BRDF snippets and `OrbitLightRig`.
- `osgx/IBL.hpp` — environment-map loading, BRDF-LUT baking (including the process-wide
  `sharedBRDFLUT()` cache), SH9/Lambertian diffuse irradiance, and cubemap readback helpers
  (`readCubeMapFaces()`, `BRDFLUTReadback`).
- `osgx/CaptureCubeMap.hpp` — `CaptureCubeMapScene`, the low-level frame-driven reflection-probe
  primitive (six ordered FBO cameras capturing a caller-owned scene into a radiance cubemap).
- `osgx/GGXPrefilter.hpp` — GPU GGX prefilter scene construction, rebaking, and readback.
- `osgx/LambertianBake.hpp` — frame-driven GPU Lambertian/diffuse cubemap baking and readback
  (`LambertianBakeScene`, `LambertianCubeReadback`).
- `osgx.hpp` — convenience umbrella that includes all of the above (but not `osgx::debug` or
  `osgx::imgui` — see below).
- `osgx/Version.hpp` — `OSGX_VERSION_MAJOR`/`MINOR`/`PATCH` and the `OSGX_VERSION` string, generated
  from the CMake project version.

> [!NOTE]
> If you want to avoid having to `make install` before testing locally, you can
> use a command like the following: `export OSG_LIBRARY_PATH="$PWD/plugins/gltf:$PWD/plugins/ktx2${OSG_LIBRARY_PATH:+:$OSG_LIBRARY_PATH}"`
> This will ensure your local build's `osgdb_{ktx2,gltf}` files are used
> instead.

## Shader libraries

`osgx/Shader.hpp` provides a reusable GLSL-snippet catalog rather than a project-specific
search-and-replace pass. Register one or more catalogs, then expand pragmas in shader source:

```cpp
osgx::pbr::registerShaderLibs();
osgx::ibl::registerShaderLibs();

const osgx::ShaderLib appLibs[] = {
	{"common", {}, COMMON_GLSL},
	{"lighting", "appLighting", LIGHTING_GLSL}
};

osgx::registerShaderLibs("myApp", appLibs);

auto glsl = osgx::resolveShaderLibs(R"GLSL(
#pragma myApp common, lighting
#pragma osgx::pbr F_SCHLICK, IBL_SPECULAR
#pragma osgx::ibl *
)GLSL");
```

Registered namespace and library names are case-insensitive. A pragma accepts comma-separated
library names, optional GLSL-function aliases, and `*` to expand the entire catalog in
registration order. Unknown names in a registered namespace fail immediately; unrelated pragmas
are left intact for other shader tooling or the GLSL compiler.

- `OSGX_DISABLE_WARNINGS` / `OSGX_ENABLE_WARNINGS` silence noisy OSG headers with compiler-specific diagnostic push/pop macros.
- `ObjectPath` is a simple dotted path accumulator used by scene-description visitors.
- `vec_t` and the `_v`, `_sz`, `_z` literals provide compact OSG scalar and size literals.
- `OSGReferenced`, `OSGArray`, and `OSGDrawElements` are concepts that constrain helpers to the expected OSG base types.
- `make_ref<T>()` and `make_nref<T>(name, ...)` create `osg::ref_ptr` objects, optionally naming the object immediately.
- `tick` and `call()` provide lightweight `osg::Timer`-based timing around arbitrary callables.
- `getFirstParent<T>()` walks an OSG parent chain and returns the first parent matching a requested type.
- `ring_buffer<T, N>` and `aring_buffer<T, N>` keep fixed-size recent samples, with the arithmetic version adding averages.
- `findDataFile()` wraps the `osgDB` file utils, letting you specify multiple paths/suffixes in one call.
- `LambdaVisitor`, `IndexedVisitor`, `NameVisitor`, and `DescribeSceneVisitor` make common scene-graph traversal, naming, and tree-printing tasks less boilerplate-heavy.
- `VisitorEventHandler` runs a visitor against the viewer scene root when a key is released.
- `LambdaKeyHandler` binds one or more key presses to a lambda with optional access to the pressed key.
- `FilterNotifyHandler` filters known-noisy OSG notify messages with regexes before writing the rest to stderr.
- `Array<BaseArray>` wraps OSG array types with initializer/range constructors, append helpers, `std::span`, and range views; aliases include `Vec2Array`, `Vec3Array`, `Vec4Array`, and `FloatArray`.
- `DrawElements<BaseElements>` wraps indexed primitive sets with modern constructors, append helpers, primitive factories, bounds-checked pushes, spans, and views; aliases cover UByte, UShort, and UInt indices.
- `WriteTextureCallback` lets a camera draw callback write a texture to disk on demand.
- `CallbacksGroup` and its camera/node/drawable aliases fan out one OSG callback slot to multiple callbacks.
- `CameraDrawLambdaCallback` and `NodeLambdaCallback` adapt lambdas to common OSG callback types.
- The object-ID picking helpers create pick cameras, encode/decode 32-bit IDs, choose IDs from pick regions, and support sync or PBO-backed async readback.
- `PickCameraSync`, `PickHoverCallback`, and `PickHandler` wire pick cameras and readback state into viewer camera updates, hover transitions, and mouse input.
- `MultiCameraManipulator` routes input to one active manipulator while letting each target drive either the main viewer camera or its own camera, useful for RTT and multi-view tools.
- `Ortho2DManipulator` is an orthographic 2D camera manipulator with pan, geometric zoom, pixel-nudge zoom, optional Ctrl-drag 3D tilt, and automatic near/far projection setup.
- `OrbitAxisManipulator` is a "turntable" model-viewer camera manipulator: it orbits a fixed vertical axis through the model's bounds, always looking level, with mouse move/drag always active (no button needed) and dolly zoom clamped by viewport coverage. `orbitByDelta(dx, dy)` applies a pre-computed delta directly, and `setLiveOrbitEnabled(false)` disables its own raw-cursor tracking -- together the hooks for composing with an external mouse-capture scheme like `osgx::platform::PointerCapture` (see `examples/osgx-manipulator.cpp`'s `OrbitCaptureBridge`) without giving the manipulator a dependency on `osgx::platform`.
- `Grid` draws a procedurally generated, antialiased grid as either a screen-space overlay or a perspective ground plane.
- `osgx::pbr` provides reusable BRDF GLSL snippets and `OrbitLightRig` for direct-light uniforms.
- `osgx::ibl` provides reusable IBL GLSL snippets plus helpers for BRDF-LUT baking (`sharedBRDFLUT()`), GPU GGX-prefiltered/Lambertian environment baking (`GGXPrefilterScene`, `LambertianBakeScene`), a generic reflection-probe primitive (`CaptureCubeMapScene`), and SH9 diffuse irradiance.

glTF-specific material and rendering integration lives with the loader in `osgx::gltf` (see below).
Generic osgx does not depend on or duplicate its public shader interface.

# `osgx::debug` — GL_KHR_debug + profiler

`osgx/Debug.hpp` provides three independent systems that can be used together or separately. It is
not included by `osgx.hpp`; opt in explicitly with `#include "osgx/Debug.hpp"`.

For a Dear ImGui front-end, see `osgx/ImGui.hpp` / `osgx::imgui::Widget` below — it is a sibling of
`osgx::debug`, not nested under it, since most of it (sliders, `Panel`, texture/stats browsers) has
nothing to do with `GL_KHR_debug` specifically; only its `ProfilerSection` reaches into
`osgx::debug`.

---

## System 1 — GL Driver Message Callback

`glDebugMessageCallback` is the GL driver speaking *to you*. When the driver detects
an error, a performance warning, or a shader issue, it calls your registered function.
This is a completely separate pipe from the other two systems — no queries, no groups,
just the driver reporting what it observed.

- `osgx::debug::initialize(gc)` — resolves all `GL_KHR_debug` extension pointers for a context.
- `osgx::debug::installDefaultCallback()` — registers a callback that writes driver messages to `osg::notify`.
- `osgx::debug::setCallback(fn)` / `clearCallback()` — install or remove a custom callback.
- `osgx::debug::enableDebugOutput()` / `disableDebugOutput()` — toggle `GL_DEBUG_OUTPUT[_SYNCHRONOUS]`.
- `osgx::debug::GraphicsOperation` — a `setRealizeOperation()` helper that calls `initialize()` once the context exists.

---

## System 2 — KHR Debug Group Annotations

`glPushDebugGroup` / `glPopDebugGroup` are *you* speaking to the GL driver — and by
extension to tools like apitrace and RenderDoc that intercept those calls. Annotations
appear as named, nested regions in a trace, making it immediately clear which draw calls
belong to which logical pass.

**Critical rule:** all calls require a current GL context. Never call `pushGroup` or
`messageInsert` from application-side C++ code (e.g. from `main()` or a viewer loop);
always use the camera callback objects, which fire while the context is guaranteed current.

**Annotations are camera-scoped.** A `PRE_DRAW` / `FINAL_DRAW` callback pair brackets
everything that camera renders — not individual nodes or groups within the scene graph.
Sub-groups of a single camera's scene cannot be individually annotated this way; for
per-group bracketing, each group needs its own RTT camera. For per-drawable bracketing,
use `ProfilerCallback` instead.

- `osgx::debug::Scoped` — **use this by default.** RAII push/pop; construct on the stack inside any `drawImplementation` or draw callback. Begin and end are in the same scope.
- `osgx::debug::AnnotationGroup` — use only when begin and end must live in *separate* callback objects. Its sole purpose is to share push/pop state across `AnnotationBeginCallback` (`PRE_DRAW`) and `AnnotationEndCallback` (`FINAL_DRAW`) so the group remains matched across the camera's frame boundary. Not a general-purpose annotation type.
- `osgx::debug::AnnotationBeginCallback` / `AnnotationEndCallback` — install an `AnnotationGroup` on a camera's `PRE_DRAW` or `FINAL_DRAW` slot so it fires at the correct time.
- `osgx::debug::appendCameraDrawCallback()` / `prependCameraDrawCallback()` — safely add callbacks to an existing slot without clobbering others.

Groups nest naturally. `FrameByFrameViewer` installs a single `Frame` group on its
camera automatically. Any other object with its own RTT camera (e.g. an `osgSlug::Atlas`)
can install its own `AnnotationBeginCallback` / `AnnotationEndCallback` and it will
appear nested inside the viewer's group in the trace:

```
Frame
  Atlas: glyph-cache
    [draw calls]
  Atlas: labels
    [draw calls]
/Frame
```

---

## System 3 — GPU Timestamp Profiler

`glQueryCounter` wraps each drawable in a pair of GPU timestamp queries so you can
measure actual GPU cost per draw call, averaged over a ring buffer of recent frames.
This is a two-phase design to avoid stalling the GPU pipeline mid-submission.

**Phase 1 — submit** (`ProfilerCallback`, per drawable):
```
glQueryCounter(begin) → draw → glQueryCounter(end) → push to FrameAccumulator
```
Never blocks. Install with `ProfilerVisitor` or attach `ProfilerCallback` directly.

**Phase 2 — drain** (`ProfilerFinalCallback`, per camera):
```
after all draws: read GL_QUERY_RESULT → update ring buffers → print/report
```
Blocks only on the last outstanding query. Install on the camera's `FINAL_DRAW` slot.

- `osgx::debug::ProfilerCallback<N>` — drawable wrapper; issues timestamp queries and measures CPU time.
- `osgx::debug::ProfilerVisitor<N>` — walks the scene graph and installs `ProfilerCallback` on every drawable.
- `osgx::debug::ProfilerFinalCallback<N>` — camera callback that drains `FrameAccumulator` results.
- `QueryMode::SYNC` (default) — blocks per-query; suitable for step-by-step viewers.
- `QueryMode::ASYNC` — drains the previous frame's results with zero meaningful stall; use for continuous renderers.

---

## Data flow summary

```
GL driver ──glDebugMessageCallback──► your callback (errors, warnings)

you ──AnnotationBegin/EndCallback──► GL driver ──► apitrace / RenderDoc

ProfilerCallback ──► FrameAccumulator ──► ProfilerFinalCallback ──► osg::notify (or osgx::imgui::Widget)

FrameByFrameViewer::requestRender() ◄── 'n' key (or an osgx::imgui button)
```

The three flows are independent. Annotations never need to know the profiler exists,
and the driver callback has nothing to do with either.

---

## `FrameByFrameViewer`

A single, consolidated `osgViewer::Viewer` subclass for apitrace-friendly debugging.
Press **`n`** to advance one frame; the viewer idles between keystrokes so a capture
session does not flood with unintended frames. On first render it automatically installs
`Frame` / `Render` annotation groups on the camera (System 2). Combine with
`ProfilerVisitor` + `ProfilerFinalCallback` (System 3) and `installDefaultCallback()`
(System 1) for a fully instrumented scene.

---

## `osgx::imgui` — Dear ImGui overlay

`osgx/ImGui.hpp` (gated behind `OSGX_IMGUI`, requiring Dear ImGui via pkg-config; set via the
`OSGX_WITH_IMGUI` CMake option, which compiles it into `libosgx` when Dear ImGui is found)
provides
`osgx::imgui::Widget`, a self-contained Dear ImGui overlay with pluggable sections
(`addStatsSection()`, `addProfilerSection()`, `addTextureSection()`, or a custom
`addSection(label, fn)`), and `osgx::imgui::Panel`, the same section machinery for an
application that already owns its own ImGui context/frame/window.

---

## `osgx::platform` — X11/EGL/GBM window helpers

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

---

# `osgx::gltf` — glTF 2.0 loader + PBR/IBL adapter

Basic glTF 2.0 mesh/texture support for OpenSceneGraph. As of the last full Khronos sample sweep,
works for all of [the Khronos samples](https://github.com/KhronosGroup/glTF-Sample-Models).

![osgx::gltf render compared with BabylonJS](ext/github/compare-babylonjs.png)

The screenshot compares this renderer's PBR/IBL output against BabylonJS using the same model and
environment lighting. The goal is not pixel-perfect matching, but matching the material response
closely enough that reflections, roughness, and HDR environment lighting behave like a modern glTF
renderer should.

This code **started** as a patched version of the
[osgEarth](https://github.com/gwaldron/osgearth/tree/master/src/osgEarthDrivers/gltf) reference
implementation, but has since evolved into something entirely different — more features and
Khronos parity, but not (yet) exporting state that matches the osgEarth shader pipeline. It defines
both a "contract" (for custom renderers, via `osgx/gltf/Shader.hpp`) and a "ready-to-use" set of
helper functions (`osgx::gltf::pbribl`).

`osgx::gltf` and its optional `osgx::gltf::pbribl` adapter were merged in from the formerly separate
`osgGLTF` repo — same reasoning as `osgx::ktx2` above, just a larger piece: the loader already
depended on `osgx::core`, and the PBR/IBL adapter already depended on the full `osgx::osgx` layer,
so keeping them in a separate repo bought nothing but cross-repo build/version friction.

Be sure to call `osgDB::Registry::instance()->addFileExtensionAlias("glb", "gltf");` if you want to
support GLB loading through the same plugin registration path as `.gltf`.

## CMake

`OSGX_BUILD_GLTF` (default ON at the top level, OFF when embedded) gates two static-library
targets, plus the `osgdb_gltf` osgDB plugin:

- `osgx::gltf` — the loader itself. Depends only on `osgx::core` and the vendored `ext/tinygltf`
  submodule. This is what `osgdb_gltf` links, so loading a `.gltf`/`.glb` through generic
  `osgDB::readNodeFile()` never drags in the rest of `osgx::osgx` (X11, ImGui, the debug profiler,
  etc.) just to parse geometry.
- `osgx::gltf_pbribl` — the optional PBR/IBL adapter (`osgx/gltf/PBRIBL.hpp`). Links `osgx::gltf` plus the
  full `osgx::osgx` PBR/IBL layer. Only an explicit consumer pays for this.

```cmake
find_package(osgx CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE osgx::gltf)       # loader only
target_link_libraries(my_viewer PRIVATE osgx::gltf_pbribl)   # + PBR/IBL renderer
```

```cpp
#include <osgx/gltf/Reader.hpp>

osgx::gltf::Reader reader;
auto result = reader.read(path, isBinary, options);
```

`osgdb_gltf` and the Python module both link the same `osgx::gltf` target; neither recompiles the
reader implementation or instantiates its own copy of tinygltf/STB source.

## Shader Interface

`osgx::gltf` loads geometry and materials without imposing a particular renderer. The public
`osgx/gltf/Shader.hpp` header defines the attribute locations, buffer bindings, texture units,
uniform names, and canonical GLSL material declaration populated by the loader.

Custom renderers can use the GLSL declaration directly and apply the matching program setup:

```cpp
#include <osgx/gltf/Shader.hpp>

auto program = new osg::Program();
auto stateSet = model->getOrCreateStateSet();

// Add shaders using osgx::gltf::shader::MATERIAL_INPUTS as part of the fragment source.
osgx::gltf::shader::configureProgram(*program);
osgx::gltf::shader::configureStateSet(*stateSet);

stateSet->setAttributeAndModes(program);
```

`configureProgram()` maps the tangent and skinning inputs to the locations used by the loader.
`configureStateSet()` maps the material samplers to the loader's base-color, normal, ORM, and
emissive texture units. These helpers are optional; the named constants in the same header can be
used when an application needs different program or StateSet ownership.

The Python bindings expose the same constants, GLSL source, and helpers under `osgx.gltf.shader`.

## Optional PBR/IBL Renderer

`osgx::gltf::pbribl` applies `osgx::gltf`'s material interface using the generic facilities in
`osgx::pbr` and `osgx::ibl`. The loader target remains shader-agnostic; applications opt into this
renderer explicitly via `osgx::gltf_pbribl` (see CMake above):

```cpp
#include <osgx/gltf/PBRIBL.hpp>

auto environment = osgx::gltf::pbribl::preparePBRIBLEnvironment(ktx2Path, hdrPath);
auto scene = osgx::gltf::pbribl::createPBRIBLScene(model, environment);

if(environment.valid() && scene.valid()) {
	root->addChild(environment.root);
	root->addChild(scene.node);
}
```

The material GLSL helpers are registered under `#pragma osgGLTF ...` — the GLSL-side registry key
and uniform/attribute names (`osgGLTF_Material`, `osgGLTF_textures`, etc.) intentionally still use
the pre-merge name; only the C++ namespace and CMake targets were renamed to `osgx::gltf`, since
the shader ABI is a separate, wider-blast-radius decision left for later. Their canonical material
declaration comes directly from `osgx/gltf/Shader.hpp`. Python exposes the same API under
`osgx.gltf.pbribl`. `utils/osgx-gltf-viewer` is the corresponding complete C++ consumer.

## PBR/IBL environment baking

`utils/osgx-pbribl` turns one HDR equirectangular image into a complete
`osgx_pbribl` environment bundle consumable by `utils/osgx-gltf-viewer --env`:

```bash
utils/osgx-pbribl input.hdr environments/studio
utils/osgx-gltf-viewer --env environments/studio.gltf model.gltf
```

The command writes `studio-specular.ktx2`, `studio-diffuse.ktx2`, and `studio.gltf`. The manifest
refers to its two KTX2 files by basename, so keep them beside the generated `.gltf` file (or move
the whole bundle together).

The specular and diffuse cubemaps are derived from the HDR. The manifest identifies the built-in
split-sum GGX BRDF LUT and its size instead of writing an HDR-independent LUT file; the renderer
caches and bakes that LUT once per process. Existing manifests that provide a BRDF-LUT `uri` remain
supported.

Optional quality controls are `--prefilter-size`, `--samples`, `--diffuse-cube-size`,
`--diffuse-samples`, and `--lut-size`.

| Output | `osgx-pbribl` default | Khronos Sample Viewer reference default |
| --- | ---: | ---: |
| GGX specular cubemap base size (`--prefilter-size`) | 128 | 256 |
| GGX samples per texel (`--samples`) | 1024 | 1024 |
| Lambertian diffuse cubemap size (`--diffuse-cube-size`) | 256 | 256 |
| Lambertian samples per texel (`--diffuse-samples`) | 2048 | 2048 |
| BRDF LUT size (`--lut-size`) | 1024 | 1024 |
| BRDF LUT integration samples | 512 (fixed) | 512 |

`osgx-pbribl` emits the full specular mip chain down to 1×1. The Khronos reference environment
uses an eight-level GGX chain; this mainly affects the roughest lookup levels.

---

# Extensions

- [GL_KHR_debug](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt)
- GL_ARB_debug_output
- GL_EXT_debug_marker
- GL_EXT_debug_label
- GL_AMD_debug_output

# Examples

```
export LSAN_OPTIONS=suppressions=/home/cubicool/dev/osgdebug/OSG/lsan.supp
export OSG_GL_CONTEXT_VERSION=3.0
```

`osgx-ibl` is the generic visual laboratory for IBL outputs. It currently bakes a Lambertian
diffuse cubemap directly from an HDR panorama, then displays it either as a Z-up skybox or a
literal six-face GL cubemap cross:

```bash
./examples/osgx-ibl Cannon_Exterior.hdr --mode lambertian --size 256 --samples 2048
```

Press `c` to switch between the diffuse skybox and cross views, or `p` to inspect the original
tone-mapped equirectangular HDR panorama. Future generic source/GGX/LUT modes belong in this tool
rather than in glTF-specific examples.

`osgx-callback`, `osgx-notifyfilter`, and `osgx-viewer` demonstrate the `osgx::debug` systems
above (`FrameByFrameViewer`, `ProfilerVisitor`/`ProfilerFinalCallback`, `DescribeSceneVisitor`).
`osgx-imgui` and `osgx-imgui-external` demonstrate `osgx::imgui::Widget` and the app-owned
`osgx::imgui::Panel` pattern, respectively. `osgx-drawables` (in `utils/`) attaches
`ProfilerCallback` via `ProfilerVisitor` to a loaded model from the command line.

`osgx-platform` demonstrates `osgx::platform`: prints the real XRandR monitor layout, moves and
pins the (default GLX) window via `moveWindow()`/`alwaysOnTop()`, and, when built with
`OSGX_WITH_EGL`/`OSGX_WITH_GBM`, drives the same scene through `--egl` or `--gbm` instead.

`osgx-ktx2-skybox` (in `examples/`) loads a KTX2 cubemap and displays it as a skybox, stepping
through the mip chain interactively — useful for verifying a generated specular or diffuse cube.

`utils/osgx-gltf-viewer` is the full glTF PBR/IBL reference viewer (`osgx::gltf::pbribl`);
`utils/osgx-pbribl` generates its static `osgx_pbribl` environment bundles.
