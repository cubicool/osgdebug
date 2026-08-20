# `osgx` core — everything not under its own namespace

Reference for the always-available, non-namespaced parts of `osgx.hpp` — the classes and helpers
that live directly in `osgx::`, one section per header. For the opt-in, explicitly-`#include`d
subsystems (each with its own C++ namespace), see [DEBUG.md](DEBUG.md), [IMGUI.md](IMGUI.md),
[PLATFORM.md](PLATFORM.md), and [GLTF.md](GLTF.md) instead.

## `osgx/Core.hpp`

- `OSGX_DISABLE_WARNINGS` / `OSGX_ENABLE_WARNINGS` silence noisy OSG headers with compiler-specific diagnostic push/pop macros (`__clang__` branch first, then `__GNUC__`, then a no-op fallback).
- `ObjectPath` — a `std::list<std::string>` dotted-path accumulator (`.str()` joins with `.`), used by `DescribeSceneVisitor` to track scene-graph name hierarchies.
- `vec_t` (`osg::Vec3::value_type`) and the `_v`/`_sz`/`_z` literals provide compact OSG scalar and `std::size_t` literals.
- `OSGReferenced`, `OSGArray`, and `OSGDrawElements` are concepts that constrain helpers to the expected OSG base types.
- `make_ref<T>(args...)` / `make_ref<T>(nullptr)` and `make_nref<T>(name, args...)` create `osg::ref_ptr` objects, optionally naming the object immediately.
- `tick` and `call(func, args...)` provide lightweight `osg::Timer`-based timing around arbitrary callables, returning `std::pair<optional<Result>, delta_ticks>`.
- `getFirstParent<T>()` walks an OSG parent chain and returns the first parent matching a requested type.
- `ring_buffer<T, N>` and `aring_buffer<T, N>` keep fixed-size recent samples, with the arithmetic version adding `.average()` (all valid samples) and `.average(count)` (last N). `RING_BUFFER_T(T, N)` exposes protected members in subclasses via `using`.
- `findDataFile()` wraps the `osgDB` file utils, letting you specify multiple paths/suffixes in one call.

## `osgx/Visitors.hpp`

- `LambdaVisitor<Node>` — `NodeVisitor` wrapping `std::function<void(Node&)>`.
- `IndexedVisitor` — tracks traversal depth in `_i`; use `itraverse()` instead of `traverse()`.
- `NameVisitor` — auto-assigns `$ClassName_N` names to unnamed nodes (`CLASS`/`PATH`/`FORCE` options).
- `DescribeSceneVisitor` — prints the scene tree to stdout with indentation and dot-path tracking.
- `VisitorEventHandler<Visitor, Viewer>` — runs a visitor against the scene root when a key is pressed.
- `LambdaKeyHandler` — wires one or multiple keys to a lambda `(ea, aa[, key]) -> bool`.
- `FilterNotifyHandler` — an `osg::NotifyHandler` that suppresses known-noisy OSG messages by regex (draw()/cull() spam, BufferObject release messages) before writing the rest to stderr.

## `osgx/Array.hpp`

- `Array<BaseArray>` wraps `osg::Vec2Array`/`Vec3Array`/`Vec4Array`/`FloatArray` (aliased directly under those names in `osgx::`) with:
  - Constructors from initializer-list, an input range, variadic args (each convertible to the element type), or — since 2026-08-18 — an element *count* (`explicit Array(std::size_t count)`, preallocates default-constructed elements, matching `BaseArray`'s own sized constructor). For `osgx::FloatArray` specifically, a bare `int`/`unsigned` literal still prefers the variadic single-element constructor over the sized one (exact-match template deduction beats the `int→size_t` conversion) — pass an actual `std::size_t` (e.g. the `_sz` UDL) to select the sized constructor unambiguously there. Non-arithmetic element types (`Vec2/3/4Array`) have no such ambiguity.
  - `append_range()`, `append_n<N>()` (compile-time count), `append_n(value, n)` (runtime count).
  - `span()` / `span(start, count)` — `std::span` views (mutable or const).
  - `view()` / `view(start, count)` — `std::ranges::subrange` views.
  - `static create(...)` factory returning `osg::ref_ptr<Array>`.
  - Fully interchangeable with native `osg::*Array` — same layout, same serialization, `dynamic_cast`/`static_cast` compatible in both directions (see `examples/osgx-array.cpp`).
- `DrawElements<BaseElements>` wraps `osg::DrawElementsU{Byte,Short,Int}` (aliased as `DrawElementsUByte`/`UShort`/`UInt`) with:
  - Constructors from `GLenum mode` + initializer-list / range / variadic.
  - `append()`, `append_range()`, `checked_push()` (bounds-checked, throws on overflow).
  - Static factories: `triangles()`, `lines()`, `strip()`, `fan()`.
  - `span()`, `view()`, `static create(...)`.

## `osgx/Callbacks.hpp`

- `CallbacksGroup<Callback>` — composite callback that fans out to multiple registered callbacks, in list order, side by side (**not** chained via `Callback::setNestedCallback`). Aliased as `CameraDrawCallbacksGroup`, `NodeCallbacksGroup`, `DrawableDrawCallbacksGroup`.
  - `add(cb)`/`remove(cb)` — identity-based.
  - `size()`/`get(i)`/`set(i, cb)`/`removeAt(i)`/`insert(i, cb)` — index-based, added so bindings can expose a real sequence proxy instead of `py::dynamic_attr()`.
- `LambdaCallbackBase<Callback, Fn>` + concrete `CameraDrawLambdaCallback` / `NodeLambdaCallback` — adapt a lambda to the matching OSG callback type.
- `WriteTextureCallback` — a `Camera::DrawCallback` that asynchronously writes a texture to disk on demand (`.write(filename)`, atomic-`bool`-flag triggered).

## `osgx/Picking.hpp`

Texture-based object-ID picking: an RTT camera renders a flat "pick ID" shader (1-based; 0 =
background), encoded as 32-bit RGBA.

- `makePickCamera(w, h, image*)` / `makePickCamera(w, h, Texture2D*)` — assembles the pick camera (shader, `BlendFunc`, small-feature culling disabled, `ABSOLUTE_RF`).
- `decodePickID(px)` — decodes all 4 RGBA bytes into a 32-bit ID.
- `PickRule` (`std::function<uint32_t(const uint8_t*, int)>`) — `spiralPick` (default), `pickCenter`, `pickMostCoverage`, `pickNearestToCenter`.
- `PickReadback` — shared atomic state (`onPick`/`onEnter`/`onLeave`, mouse position, last ID); `PickReadbackSync`/`PickReadbackAsync` are the SYNC (`osg::Image` readback) and ASYNC (`Texture2D` + PBO + `glGetTexImage`) variants, each with `Mode::CLICK`/`Mode::CONTINUOUS`.
- `PickCameraSync` — syncs the pick camera's view/projection from the viewer camera every update traversal (with an optional 1×1 sub-frustum for continuous hover).
- `PickHoverCallback` — polls `lastID()` on the update thread and fires `onEnter`/`onLeave` on transitions; the correct way to trigger scene-graph mutation from a hover event.
- `PickHandler` — routes click/move events to the readback (`continuous=true` for hover, `consumeEvents=true` for exclusive picking).

See `examples/osgx-picking.cpp` (full SYNC/ASYNC × click/continuous matrix) and `examples/osgx-hover.cpp` (`onEnter`/`onLeave` driving real scene mutation).

## `osgx/Manipulators.hpp`

- `MultiCameraManipulator` — composite manipulator routing input to one active `Target` (name, manipulator, optional dedicated camera/scene, optional `setActive` callback); `addTarget()`, `activate(index)`/`next()`, `getActiveIndex()`/`getNumTargets()`, key-toggle via `setToggleKey()` (default `'x'`).
- `Ortho2DManipulator` — orthographic 2D camera manipulator owning both view *and* projection matrices. Pan (drag), geometric zoom (scroll), pixel-nudge zoom (Shift+scroll), optional Ctrl-drag 3D tilt (yaw/pitch tracked as independent angles, pitch clamped to ±89°), automatic near/far. Unrotated plane configurable via `setPlaneNormal()`/`setScreenUp()` (default XY, +Z normal).
- `OrbitAxisManipulator` — a "turntable" manipulator: orbits a fixed vertical guide line through the model's bounds, always looking level, dollying on zoom. Mouse move/drag is always active (no button needed), bounded like a trackpad by the screen edge unless composed with `osgx::platform::PointerCapture` via `orbitByDelta()`/`setLiveOrbitEnabled(false)`. Orientation configurable via `setUpAxis()`/`setHomeDirection()` (default Z-up, from -Y).
- `CameraManipulator<Base=osgGA::TrackballManipulator>` — wraps any `osgGA::CameraManipulator` with `addUpdateCameraCallback(osg::Callback*, runOnce)` / `removeUpdateCameraCallback()`, an async-apply queue (safe to mutate mid-callback-iteration), and `currentTime()` sourced from the FRAME event (not a polled `osg::Timer`) for [`osgx::CameraIntents`](#osgxcameraintentshpp) to read.

## `osgx/CameraIntents.hpp`

Plain `osg::Callback` subclasses meant to be attached via `CameraManipulator<Base>::addUpdateCameraCallback()`, driven by real `osgAnimation::Motion`/`CompositeMotion` timing rather than hand-rolled elapsed/duration math.

- `Viewpoint` — `{eye, center, up}` (`up` defaults to `+Z`).
- `FlyToCallback` — animates the camera through one or more `Viewpoint` legs (eye lerped, orientation slerped — never lerping two `lookAt()` centers directly). `osgAnimation::Motion::CLAMP` (default): on arrival, writes the exact final pose, resyncs the manipulator via `setByMatrix()`, then goes permanently inert. `Motion::LOOP`: the whole path repeats forever (author a waypoint list whose ends coincide for a seamless loop — same convention as `osg::AnimationPath`). `ease` is any `float(float)` callable (`defaultEase` = `InOutCubicFunction`), shared across every leg.
- `ShakeCallback` — decaying rotational jitter, right-multiplied onto whatever's already in the camera's view matrix (never touches the manipulator's own state). `CLAMP` (default) decays once and goes inert; `LOOP` repeats for a persistent "idle rumble."

See `examples/osgx-manipulator.cpp` for patrol (`LOOP`) and arrival-latch usage, and `docs/PLATFORM.md` for composing `OrbitAxisManipulator` with `PointerCapture`.

## `osgx/Grid.hpp`

- `Grid` draws a procedurally generated, antialiased grid as either a screen-space overlay or a perspective ground plane.

## `osgx/Shapes.hpp`

- `VertexLayout` — the generic-attribute locations (position/normal/uv) a generated `Polyhedron` installs, both through Geometry's conventional arrays (bounds/compatibility) and as explicit generic attributes (core-profile shaders).
- `Polyhedron` — an `osg::Geometry` built from `vertices` + `Face` list (each face: vertex indices + optional per-corner UVs). `rebuild()` mutates the existing backing arrays in place rather than replacing them (same "don't reallocate every frame" lesson `osgx::LightMarkers` reuses). Per-face custom attributes via `setFaceVertexAttribute()`/`setFaceAttribute()`/`removeAttribute()`; `faceNormal()`/`faceUp()`/`restingOffset()`/`faceRestingOffset()` for placement queries; `isometricFaceUV()` static helper.
- `Cube`, `Tetrahedron`, `Octahedron`, `Icosahedron`, `Dodecahedron`, `PentagonalTrapezohedron` — concrete `Polyhedron` subclasses, each constructible from `(center, radius, layout)` (`Cube` also takes `(center, size, layout)`).

## `osgx/Shader.hpp`

Generic, line-oriented GLSL library expansion — a reusable snippet catalog rather than a
project-specific search-and-replace pass. Register one or more catalogs (`osgx::registerShaderLibs()`),
then expand `#pragma` directives in shader source via `osgx::resolveShaderLibs()`. Registered
namespace/library names are case-insensitive; a pragma accepts comma-separated library names,
optional GLSL-function aliases, and `*` to expand an entire catalog in registration order. See the
worked example in the main [README](../README.md#osgxhpp--public-headers).

## `osgx/PBR.hpp`

- Reusable BRDF GLSL snippets (`osgx::pbr` namespace — GGX distribution, Schlick Fresnel, Smith geometry) — plain function-body snippets, concatenated into a consuming fragment shader, not full shaders of their own.
- `LightSet` — owns a `std430`-SSBO-backed array of typed direct lights (`LightType::Point`/`Directional`/`Spot`; a sphere light is a `Point`/`Spot` with non-zero `sourceRadius`, not a fourth type) plus its `osgx_lightCount` uniform on an `osg::StateSet`. `LightSet::create(ss)` allocates and installs the buffer (size `MAX_LIGHTS`, zero-initialized, everything off until `setCount()`+`setPoint()`/`setDirectional()`/`setSpot()`). Typed setters/getters (`getType()`, `getPosIntensity()`, `getColor()`, `getSourceRadius()`, `getDirection()`, `getSpotAngles()`, …) replace the old parallel-`osg::Uniform`-array contract.
- `OrbitLightRig` — the animated counterpart: an `osg::NodeCallback` that writes orbiting position/intensity into a `LightSet` every update traversal (for the subset of lights that should move; a `LightSet` can be shared between a static rig and an orbiting one).

## `osgx/Gizmos.hpp`

Debug visualization for `osgx::pbr::LightSet` lights — deliberately not part of `osgx::debug`
(that's `GL_KHR_debug` integration specifically, not scene gizmos).

- `LightMarkers` — an `osg::Group` of depth-tested, real scene-space markers for point/sphere/spot lights (up to `MAX_LIGHTS`), rebuilt in place every update traversal from the live `LightSet`. Three orthogonal wireframe circles for a point/sphere light (sized to `max(sourceRadius, minMarkerRadius)`); a wireframe cone (ring + spokes) for a spot light, sized by its outer cone angle and `spotConeLength`. A directional light has no position and is never drawn here.
- `LightGizmos` — bundles `LightMarkers` with a non-depth-tested `POST_RENDER` overlay camera for directional lights (a wireframe plane + direction arrow, sized off the target scene's bounding sphere) into one addable `osg::Group`:
  ```cpp
  auto gizmos = osgx::make_ref<osgx::LightGizmos>(lights, scene, minMarkerRadius, spotConeLength);
  root->addChild(gizmos);
  ```
  `getMarkers()`/`getOverlay()` give access to the two pieces individually, for the (rarer) case where they need different parents in the scene graph.

See `examples/osgx-lights.cpp` for one shaded object cycling through every light type with live gizmo feedback.

## `osgx/IBL.hpp`, `CaptureCubeMap.hpp`, `GGXPrefilter.hpp`, `LambertianBake.hpp`

- `osgx::ibl` provides reusable IBL GLSL snippets plus environment-map loading, BRDF-LUT baking (including the process-wide `sharedBRDFLUT()` cache), SH9/Lambertian diffuse irradiance, and cubemap readback helpers (`readCubeMapFaces()`, `BRDFLUTReadback`).
- `CaptureCubeMap.hpp` — `CaptureCubeMapScene`, the low-level frame-driven reflection-probe primitive (six ordered FBO cameras capturing a caller-owned scene into a radiance cubemap).
- `GGXPrefilter.hpp` — GPU GGX prefilter scene construction, rebaking, and readback (`GGXPrefilterScene`).
- `LambertianBake.hpp` — frame-driven GPU Lambertian/diffuse cubemap baking and readback (`LambertianBakeScene`, `LambertianCubeReadback`).

glTF-specific material and rendering integration lives with the loader in [`osgx::gltf`](GLTF.md) —
generic `osgx` does not depend on or duplicate its public shader interface.

## `osgx/Version.hpp`

`OSGX_VERSION_MAJOR`/`MINOR`/`PATCH` and the `OSGX_VERSION` string, generated from the CMake
project version. Included by `Core.hpp`, so it's available transitively almost everywhere.
