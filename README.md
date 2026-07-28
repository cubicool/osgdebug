# osgDebug

osgDebug provides easy access to the various *_debug_* OpenGL extensions, allowing the code to provide additional/informative messages and annotations
that that applications like *Nsight* and *APITrace* support.

# CMake

osgDebug and osgx can be consumed directly from their source tree or as installed CMake packages.
In either case, link the appropriate imported-style target and its OpenSceneGraph include and link
requirements will be propagated to your target.

To embed the source tree in another project:

```cmake
add_subdirectory(path/to/osgdebug EXCLUDE_FROM_ALL)
target_link_libraries(my_target PRIVATE osgDebug::osgDebug)
```

When embedded, examples, utilities, Python modules, and installation rules are disabled by default.
They can be enabled individually with `OSGX_BUILD_EXAMPLES`, `OSGX_BUILD_UTILS`,
`OSGX_BUILD_PYTHON`, and `OSGX_INSTALL`.

To install osgDebug and consume it as a package, first install an already-configured build tree:

```console
cmake --install BUILD --prefix /path/to/prefix
```

Then use the installed package from the consuming project:

```cmake
find_package(osgDebug CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE osgDebug::osgDebug)
```

Pass `-DCMAKE_PREFIX_PATH=/path/to/prefix` when configuring the consuming project if the chosen
prefix is not already in CMake's search path.

The same build tree can remove exactly the files recorded by its most recent install:

```console
cmake --build BUILD --target uninstall
```

This uses CMake's generated `install_manifest.txt`; keep the build tree until the installation is
no longer needed.

Projects needing only osgx can use `osgx::core` for the low-level facilities or `osgx::osgx` for
the complete utility layer. In installed mode, use `find_package(osgx CONFIG REQUIRED)` first.
`osgx::core` is header-only. `osgx::osgx` is the compiled `libosgx` library, includes
`osgx::core`, and contains implementation-heavy facilities such as GPU GGX environment
prefiltering. `osgDebug::osgDebug` includes the complete osgx layer.

## Fresh checkout build order

For a full osgdebug + osgGLTF installation, build and install this repository first. osgGLTF's
GPU prefilter tool links the installed `osgx::osgx` target:

```text
1. Configure, build, and install osgdebug (provides osgx::core and osgx::osgx).
2. Configure osgGLTF with that installation in CMAKE_PREFIX_PATH.
3. Build and install osgGLTF.
```

There is no reverse build dependency: osgdebug does not require osgGLTF. osgGLTF consumes
`osgx::core` for its low-level infrastructure and provides its own optional adapter over the full
`osgx::osgx` PBR/IBL layer.

# `osgx.hpp` Extras

`osgx.hpp` is the umbrella header for a small C++20 OpenSceneGraph utility layer that sits beside `osgDebug.hpp`. It keeps common setup code shorter and adds modern range/span/lambda-friendly wrappers.

Its public headers are organized by concern:

- `osgx/Shader.hpp` — generic, line-oriented GLSL library expansion.
- `osgx/PBR.hpp` — PBR BRDF snippets and `OrbitLightRig`.
- `osgx/IBL.hpp` — environment-map loading, BRDF-LUT baking, and diffuse irradiance helpers.
- `osgx/GGXPrefilter.hpp` — GPU GGX prefilter scene construction, rebaking, and readback.
- `osgx/LambertianBake.hpp` — frame-driven GPU Lambertian/diffuse cubemap baking.
- `osgx.hpp` — convenience umbrella that includes all osgx facilities.

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
- 'findDataFile` wraps the `osgDB` file utils, letting you specify multiple paths/suffixes in one call.
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
- `Grid` draws a procedurally generated, antialiased grid as either a screen-space overlay or a perspective ground plane.
- `osgx::pbr` provides reusable BRDF GLSL snippets and `OrbitLightRig` for direct-light uniforms.
- `osgx::ibl` provides reusable IBL GLSL snippets plus helpers for BRDF-LUT baking, GGX-prefiltered environment maps, and SH9 or Lambertian diffuse irradiance.
glTF-specific material and rendering integration lives with the loader in osgGLTF. Generic osgx
does not depend on or duplicate osgGLTF's public shader interface.

# `osgDebug.hpp` Systems

`osgDebug.hpp` provides three independent systems that can be used together or separately.

It also includes `osgDebug::Widget`, a Dear ImGui overlay for viewer tools with pluggable sections and built-in GPU-profiler, OSG-stats, and scene-texture views.

---

## System 1 — GL Driver Message Callback

`glDebugMessageCallback` is the GL driver speaking *to you*. When the driver detects
an error, a performance warning, or a shader issue, it calls your registered function.
This is a completely separate pipe from the other two systems — no queries, no groups,
just the driver reporting what it observed.

- `osgDebug::initialize(gc)` — resolves all `GL_KHR_debug` extension pointers for a context.
- `osgDebug::installDefaultCallback()` — registers a callback that writes driver messages to `osg::notify`.
- `osgDebug::setCallback(fn)` / `clearCallback()` — install or remove a custom callback.
- `osgDebug::enableDebugOutput()` / `disableDebugOutput()` — toggle `GL_DEBUG_OUTPUT[_SYNCHRONOUS]`.
- `osgDebug::GraphicsOperation` — a `setRealizeOperation()` helper that calls `initialize()` once the context exists.

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

- `osgDebug::Scoped` — **use this by default.** RAII push/pop; construct on the stack inside any `drawImplementation` or draw callback. Begin and end are in the same scope.
- `osgDebug::AnnotationGroup` — use only when begin and end must live in *separate* callback objects. Its sole purpose is to share push/pop state across `AnnotationBeginCallback` (`PRE_DRAW`) and `AnnotationEndCallback` (`FINAL_DRAW`) so the group remains matched across the camera's frame boundary. Not a general-purpose annotation type.
- `osgDebug::AnnotationBeginCallback` / `AnnotationEndCallback` — install an `AnnotationGroup` on a camera's `PRE_DRAW` or `FINAL_DRAW` slot so it fires at the correct time.
- `osgDebug::appendCameraDrawCallback()` / `prependCameraDrawCallback()` — safely add callbacks to an existing slot without clobbering others.

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

- `osgDebug::ProfilerCallback<N>` — drawable wrapper; issues timestamp queries and measures CPU time.
- `osgDebug::ProfilerVisitor<N>` — walks the scene graph and installs `ProfilerCallback` on every drawable.
- `osgDebug::ProfilerFinalCallback<N>` — camera callback that drains `FrameAccumulator` results.
- `QueryMode::SYNC` (default) — blocks per-query; suitable for step-by-step viewers.
- `QueryMode::ASYNC` — drains the previous frame's results with zero meaningful stall; use for continuous renderers.

---

## Data flow summary

```
GL driver ──glDebugMessageCallback──► your callback (errors, warnings)

you ──AnnotationBegin/EndCallback──► GL driver ──► apitrace / RenderDoc

ProfilerCallback ──► FrameAccumulator ──► ProfilerFinalCallback ──► osg::notify (or future ImGui panel)

FrameByFrameViewer::requestRender() ◄── 'n' key (or future ImGui button)
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

# Extensions

- [GL_KHR_debug](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt)
- GL_ARB_debug_output
- GL_EXT_debug_marker
- GL_EXT_debug_label
- GL_AMD_debug_output

# Examples

```
export LSAN_OPTIONS=suppressions=/home/cubicool/osgdebug/OSG/lsan.supp
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
