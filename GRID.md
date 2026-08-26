# Grid follow-up

This note carries the Grid work into the next session. The current implementation is committed as
`615d767 Add reusable Grid shader library`; the Sketchfab consumer update is committed in
OpenSceneGraph.py as `780eae3 Use osgx Grid shader library for room`.

## Current state

`osgx::Grid` still provides the original drawable plane and now also `createSphere()`. Its
fragment logic is registered as:

```glsl
#pragma osgx::grid GRID
```

The imported code declares the Grid inputs and exposes:

```glsl
vec4 osgx_GridColor(vec2 gridPos);
```

`examples/osgx-grid.cpp dice` is the direct reuse proof: it renders an `osgx::Icosahedron` with
a small vertex adapter from its face-local UVs to `gridPos`, and an importing fragment shader.
`examples/pyosg-lighting/11-sketchfab.py` resolves the same library inside its deferred G-buffer
fragment shader. The fullscreen infinite-floor mode intentionally remains separate because it
also does ray/plane intersection, depth reconstruction, and horizon/distance fading.

## Replace Grid::configureStateSet() with GridSettings

`Grid::configureStateSet(osg::StateSet*)` was removed because it was not the API shape we want. It made a
style payload look like a procedure that mutates unrelated state. `Material` and `LightSet` show
the better pattern: a reusable `osg::StateAttribute` attached normally to a StateSet.

Add a separate `osgx::GridSettings : public osg::StateAttribute`; do not turn `Grid` itself into a
StateAttribute. `Grid` remains a useful Geometry-derived plane/sphere factory, while `GridSettings`
is the reusable appearance data that can apply to a Polyhedron or any other geometry.

Desired use:

```cpp
auto settings = osgx::make_ref<osgx::GridSettings>();

settings->setCanvasSize(osg::Vec2(12.0f, 12.0f));
settings->setGridInterval(2.0f);
settings->setLineMode(osgx::GridSettings::LINE_GRID_UNITS);

shape->getOrCreateStateSet()->setAttributeAndModes(settings);
```

`Grid` should own and attach one `GridStyle` itself. Its existing setters (`setGridInterval()`,
`setColorLine()`, and so on) can remain source-compatible forwarding conveniences, ideally with
`getStyle()` access for callers that want to share or inspect it. The Python binding should expose
`GridStyle` as an `osg.StateAttribute` and retain the existing `Grid` properties.

The Grid shader library still needs a registration step before a consumer calls
`resolveShaderLibs()`. `GridStyle` construction is a reasonable place to register it, so a caller
who creates the style before resolving a shader does not need an extra `registerShaderLibs()` call.

Do not put blend state in `GridStyle`. Blending is render-pass policy, not Grid appearance:

- A normal forward Grid can enable `GL_BLEND` and `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` for a transparent background.
- A deferred G-buffer Grid must write opaque data to every attachment and leave blending off.

`Grid`'s own forward drawable setup can keep its blend state. Custom consumers choose their own
blend/depth/render-bin state, eliminating the current Sketchfab override that has to disable the
state installed by `configureStateSet()`.

## Grid shader data: uniforms, GLSL structs, UBOs, and SSBOs

SSBOs are not the only way to have a GLSL struct.

A plain struct uniform is legal GLSL:

```glsl
struct osgx_GridStyle {
	vec2 canvasSize;
	float gridInterval;
	float gridIntervalStrong;
	float lineWidthPx;
	float lineWidth;
	int edgeMode;
	int lineMode;
	vec4 colorBg;
	vec4 colorLine;
	vec4 colorLineStrong;
};

uniform osgx_GridStyle u_grid;
```

It improves shader source readability, but the driver still exposes separate member uniforms
(`u_grid.gridInterval`, etc.). It does not give a StateAttribute one atomic resource to bind, so
it would preserve most of the awkward host-side plumbing that `configureStateSet()` has today.

For this small, read-only style payload, a Uniform Buffer Object remains the most semantically
natural choice:

```glsl
struct osgx_GridStyle {
	vec2 canvasSize;
	float gridInterval;
	float gridIntervalStrong;
	float lineWidthPx;
	float lineWidth;
	int edgeMode;
	int lineMode;
	vec4 colorBg;
	vec4 colorLine;
	vec4 colorLineStrong;
};

layout(std140, binding = 4) uniform osgx_GridStyleBlock {
	osgx_GridStyle u_grid;
};
```

`GridStyle` would own the matching CPU data plus an `osg::UniformBufferBinding`; its `apply()`
binds that one object, just as `Material::apply()` and `LightSet::apply()` bind their SSBOs. Use a
binding point reserved centrally in a Grid header, rather than a magic number duplicated in GLSL
and C++.

The implementation started in this session uses an SSBO instead because it directly follows the
already-proven `Material`/`LightSet` StateAttribute pattern:

```glsl
layout(std430, binding = 4) readonly buffer osgx_GridStyleBuffer {
	osgx_GridStyle u_grid;
};
```

For fixed scalar/color style data, a UBO remains a reasonable future simplification. The Grid SSBO
is not a claim that SSBOs are the only way to express GLSL structs; it is an implementation choice
that keeps this first StateAttribute version aligned with existing osgx buffer-binding code.

Whichever block type is chosen, define and document the CPU packing against GLSL's `std140` or
`std430` rules. Do not rely on a C++ struct's incidental padding. A small float/int backing array,
with explicit offsets and comments like `Material`/`LightSet`, is safe and easy to dirty in place.

## Face orientation validation and unfolded Grid coordinates

The current icosahedron screenshot is valuable as-is: it displays each face's existing local UV
chart. The apparent rotations across edges test face winding, `Face::right()`, `Face::up()`, and
the orientation a future decal will inherit. Do not replace these existing UVs; dice-number decals
already use them and should remain unchanged.

Add an independent face-corner stream for a contiguous-as-possible grid net, for example:

```cpp
std::vector<osg::Vec2> Polyhedron::unfoldedFaceUVs(std::size_t rootFace=0) const;
```

It returns one coordinate per input face corner, exactly the format accepted by the existing
`setFaceVertexAttribute()` API:

```cpp
shape->setFaceVertexAttribute(4, new osgx::Vec2Array(shape->unfoldedFaceUVs()));
```

The Grid adapter vertex shader reads location 4 for `gridPos`; the ordinary UV attribute stays
reserved for decal texture coordinates.

Implementation outline:

1. Build face adjacency by grouping each unordered base-vertex edge. Reject or explicitly report
   non-manifold edges.
2. Place `rootFace` in 2D using its real `Face::planeCoordinates()`.
3. Traverse face adjacency breadth-first. For each newly visited neighbor, rigidly place its local
   coordinates so its shared edge agrees exactly with its parent and the polygon lies on the
   opposite side of that edge.
4. The traversal tree defines the connected portions of the net. If a previously placed face
   would obtain different coordinates through another route, mark that edge as a seam/cut rather
   than overwriting its established coordinates.
5. Expose the seam edges, or accept a caller-selected cut set later, if authoring control becomes
   useful.

Closed curved polyhedra cannot have a zero-seam, isometric global 2D chart. An icosahedron has
five equilateral triangles around each vertex (300 degrees), leaving a 60-degree angle deficit;
closing every adjacency would contradict flat coordinates. A cube can keep a familiar cube-net
set of edges continuous, but its remaining closing edges still form the net cuts. This is not an
inconsistency in existing decal UVs; it is the geometry of flattening a closed solid.

Useful follow-up demos:

- Keep `osgx-grid dice`'s current face-local mode as the orientation diagnostic.
- Add a `net` mode that consumes the new attribute and visibly marks its intentional seams.
- Try cube, tetrahedron, octahedron, icosahedron, dodecahedron, and D10, not just triangles.
- Make the seam list/debug color visible so adjacency failures are distinguishable from required cuts.
