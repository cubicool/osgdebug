#pragma once

#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Uniform>

OSGX_ENABLE_WARNINGS

namespace osgx {

// ================================================================================================
// Grid
//
// Procedurally-generated, antialiased, view-aware grid lines on a single quad. It supports crisp
// constant screen-pixel lines for flat overlays and grid/world-unit line widths for perspective
// ground planes. Both modes are derivative-driven off a "grid space" coordinate (UV * canvasSize),
// so no MVP decomposition is needed; the screen-space derivative already bakes in model, view, AND
// projection.
//
// Typical usage:
//
//   // Fullscreen background grid, always drawn first (the osgSlug HUD/UI use case):
//   auto camera = osgx::Grid::createOrthoCamera();
//   viewer.setSceneData(camera);
//   viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT); // don't let the main cam stomp it
//
//   // A real 3D ground plane instead:
//   auto grid = osgx::make_ref<osgx::Grid>(
//       osg::Vec3(-10,0,-10), osg::Vec3(20,0,0), osg::Vec3(0,0,20)
//   );
//   auto geode = osgx::make_ref<osg::Geode>();
//   geode->addDrawable(grid);
//
//   // Live updates, either way:
//   grid->setGridInterval(1.0f);
//   grid->setEdgeMode(osgx::Grid::EDGE_NUDGE);
// ================================================================================================

// Vertex shader hands the fragment shader a position in "grid space": UV (0..1) scaled by the
// logical canvas size. That keeps the grid's notion of "5 units apart" tied to the canvas the
// caller specified, independent of how big the quad actually is in NDC/world.
inline constexpr const char* GRID_VERTEX_SHADER = R"GLSL(
#version 330 core

uniform mat4 osg_ModelViewProjectionMatrix;
uniform vec2 u_canvasSize;

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

out vec2 gridPos;

void main(void) {
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;

	gridPos = osg_MultiTexCoord0 * u_canvasSize;
}
)GLSL";

inline constexpr const char* GRID_FRAGMENT_SHADER = R"GLSL(
#version 330 core

in vec2 gridPos;

uniform vec2 u_canvasSize;

uniform float u_gridInterval;
uniform float u_gridIntervalStrong; // <= 0 disables the "extra strength" tier
uniform float u_lineWidthPx;
uniform float u_lineWidth;

uniform vec4 u_colorBg;
uniform vec4 u_colorLine;
uniform vec4 u_colorLineStrong;

// Lines that fall exactly on the canvas boundary (pos == 0 or pos == u_canvasSize) only have
// geometry on one side, so their AA kernel is half-clipped and they render at roughly half
// intensity/width -- u_edgeMode picks how to handle that:
//   0 EDGE_ASIS  -- leave the half-clipped line as rendered (matches raw geometry)
//   1 EDGE_HIDE  -- don't draw boundary lines at all (clean edge, good for screen-aligned UI)
//   2 EDGE_NUDGE -- shift boundary lines inward by half their width so they render at full
//                   strength (good for a true 3D ground plane, where there's no "UI edge")
const int EDGE_ASIS = 0;
const int EDGE_HIDE = 1;
const int EDGE_NUDGE = 2;

uniform int u_edgeMode;
uniform int u_lineMode;

out vec4 fragColor;

// Antialiased coverage (0..1) of gridlines spaced `interval` apart in `pos`'s units, with the
// line drawn `lineWidthPx` screen-pixels wide regardless of view distance/angle.
float gridLine(vec2 pos, float interval, float lineWidthPx) {
	vec2 coord = pos / interval;
	vec2 deriv = fwidth(coord);

	// Guards against NaN/Inf when the derivative collapses to zero (e.g. the quad edge-on
	// to the view, or an interval of 0 from a not-yet-initialized uniform).
	deriv = max(deriv, vec2(1e-6));

	// Which line (in units of `interval`) is nearest each axis, and is that line one of the
	// two canvas-boundary lines? Computed from the raw (pre-AA-bias, pre-nudge) coordinate so
	// it reflects true geometry, independent of the pixel-alignment tricks below.
	vec2 maxCoord = u_canvasSize / interval;
	vec2 nearest = floor(coord + 0.5);
	bvec2 isMinEdge = lessThan(abs(nearest), vec2(0.5));
	bvec2 isMaxEdge = lessThan(abs(nearest - maxCoord), vec2(0.5));

	// The Cairo/Direct2D "+0.5" hairline trick: a mathematical line that lands exactly on a
	// pixel BOUNDARY gets its coverage split 50/50 across the two neighboring pixels instead
	// of drawn crisply into one, roughly doubling its apparent width. Biasing by half a screen
	// pixel re-centers that case onto a single pixel; harmless elsewhere since it's a constant
	// sub-pixel nudge of the whole grid pattern. Default bias for every non-edge fragment.
	vec2 bias = deriv * 0.5;

	// For edge lines under EDGE_NUDGE, REPLACE (not add to) the resonance bias above with the
	// nudge-inward amount -- stacking both cancels to zero on min edges (line stays on the
	// boundary, still half-clipped) and doubles up on max edges (shifts a full pixel in), which
	// is exactly the bottom/left-vs-top/right asymmetry this branch is here to avoid.
	if(u_edgeMode == EDGE_NUDGE) {
		vec2 halfWidthCoord = deriv * max(lineWidthPx, 0.0) * 0.5;

		if(isMinEdge.x) bias.x = -halfWidthCoord.x;
		else if(isMaxEdge.x) bias.x = halfWidthCoord.x;

		if(isMinEdge.y) bias.y = -halfWidthCoord.y;
		else if(isMaxEdge.y) bias.y = halfWidthCoord.y;
	}

	coord += bias;

	vec2 dist = abs(fract(coord + 0.5) - 0.5); // distance to nearest line, in grid cells
	vec2 distPx = dist / deriv; // same distance, converted to actual screen pixels
	vec2 halfWidth = vec2(max(lineWidthPx, 0.0) * 0.5);
	vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + 1.0, distPx);

	if(u_edgeMode == EDGE_HIDE) {
		if(isMinEdge.x || isMaxEdge.x) line.x = 0.0;
		if(isMinEdge.y || isMaxEdge.y) line.y = 0.0;
	}

	return clamp(max(line.x, line.y), 0.0, 1.0);
}

// Adapted from Ben Golus' "Pristine Grid" technique:
// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// `lineWidth` is in the same units as `pos`/`interval`, so perspective naturally makes lines
// thinner; once they approach sub-pixel size, they are widened in derivative space and faded by
// coverage.
float pristineGridLine(vec2 pos, float interval, float lineWidth) {
	vec2 uv = pos / interval;
	vec2 uvDDX = dFdx(uv);
	vec2 uvDDY = dFdy(uv);
	vec2 uvDeriv = max(vec2(length(vec2(uvDDX.x, uvDDY.x)), length(vec2(uvDDX.y, uvDDY.y))), vec2(1e-6));

	vec2 lineWidthCell = clamp(vec2(lineWidth / interval), vec2(0.0), vec2(1.0));
	bvec2 invertLine = greaterThan(lineWidthCell, vec2(0.5));
	vec2 targetWidth = mix(lineWidthCell, vec2(1.0) - lineWidthCell, invertLine);
	vec2 drawWidth = clamp(targetWidth, uvDeriv, vec2(0.5));
	vec2 lineAA = uvDeriv * 1.5;
	vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);

	gridUV = mix(vec2(1.0) - gridUV, gridUV, invertLine);

	vec2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);

	grid2 *= clamp(targetWidth / drawWidth, vec2(0.0), vec2(1.0));
	grid2 = mix(grid2, targetWidth, clamp(uvDeriv * 2.0 - 1.0, vec2(0.0), vec2(1.0)));
	grid2 = mix(grid2, vec2(1.0) - grid2, invertLine);

	if(u_edgeMode == EDGE_HIDE) {
		vec2 maxCoord = u_canvasSize / interval;
		vec2 nearest = floor(uv + 0.5);
		bvec2 isMinEdge = lessThan(abs(nearest), vec2(0.5));
		bvec2 isMaxEdge = lessThan(abs(nearest - maxCoord), vec2(0.5));

		if(isMinEdge.x || isMaxEdge.x) grid2.x = 0.0;
		if(isMinEdge.y || isMaxEdge.y) grid2.y = 0.0;
	}

	return clamp(grid2.x * (1.0 - grid2.y) + grid2.y, 0.0, 1.0);
}

void main(void) {
	vec4 color = u_colorBg;

	float thin = u_lineMode == 0 ?
		gridLine(gridPos, u_gridInterval, u_lineWidthPx) :
		pristineGridLine(gridPos, u_gridInterval, u_lineWidth);

	color = mix(color, u_colorLine, thin);

	if(u_gridIntervalStrong > 0.0) {
		float strong = u_lineMode == 0 ?
			gridLine(gridPos, u_gridIntervalStrong, u_lineWidthPx * 1.5) :
			pristineGridLine(gridPos, u_gridIntervalStrong, u_lineWidth * 1.5);

		color = mix(color, u_colorLineStrong, strong);
	}

	fragColor = color;
}
)GLSL";

class Grid: public osg::Geometry {
public:
	OSGX_META_Object(osgx, Grid)

	// Boundary-line handling for lines that fall exactly on the canvas edge (pos == 0 or
	// pos == canvasSize) -- see EDGE_ASIS/EDGE_HIDE/EDGE_NUDGE in GRID_FRAGMENT_SHADER above
	// for the full rationale.
	enum EdgeMode {
		EDGE_ASIS = 0,  // leave half-clipped boundary lines as rendered
		EDGE_HIDE = 1,  // don't draw boundary lines at all
		EDGE_NUDGE = 2  // shift boundary lines inward by half their width to full strength
	};

	enum LineMode {
		LINE_SCREEN_PIXELS = 0,  // constant screen-pixel hairlines, useful for HUD/ortho grids
		LINE_GRID_UNITS = 1      // perspective/world-width lines with derivative-limited AA
	};

	// Default: a fullscreen NDC quad (XY plane, z=0, -1..1) -- the most common case, meant to
	// pair with orthoCamera()/createOrthoCamera() below.
	Grid() {
		_build(osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0));
	}

	// `corner`/`widthVec`/`heightVec` place the quad in whatever space it ends up added to the
	// scene graph in -- NDC for a fullscreen overlay, or world-space for a real 3D ground plane.
	Grid(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec) {
		_build(corner, widthVec, heightVec);
	}

	Grid(const Grid& g, const osg::CopyOp& co = osg::CopyOp::SHALLOW_COPY):
	osg::Geometry(g, co) {
		_bindUniforms();
	}

	// --- Live uniform updates ---------------------------------------------------------------

	void setCanvasSize(const osg::Vec2& v) { _canvasSize->set(v); }
	void setGridInterval(float v) { _gridInterval->set(v); }
	void setGridIntervalStrong(float v) { _gridIntervalStrong->set(v); } // <= 0 disables
	void setLineWidthPx(float v) { _lineWidthPx->set(v); }
	void setLineWidth(float v) { _lineWidth->set(v); }
	void setEdgeMode(EdgeMode v) { _edgeMode->set(static_cast<int>(v)); }
	void setLineMode(LineMode v) { _lineMode->set(static_cast<int>(v)); }
	void setColorBg(const osg::Vec4& v) { _colorBg->set(v); }
	void setColorLine(const osg::Vec4& v) { _colorLine->set(v); }
	void setColorLineStrong(const osg::Vec4& v) { _colorLineStrong->set(v); }

	osg::Vec2 getCanvasSize() const { osg::Vec2 v; _canvasSize->get(v); return v; }
	float getGridInterval() const { float v = 0.0f; _gridInterval->get(v); return v; }
	float getGridIntervalStrong() const { float v = 0.0f; _gridIntervalStrong->get(v); return v; }
	float getLineWidthPx() const { float v = 0.0f; _lineWidthPx->get(v); return v; }
	float getLineWidth() const { float v = 0.0f; _lineWidth->get(v); return v; }

	EdgeMode getEdgeMode() const {
		int v = 0;

		_edgeMode->get(v);

		return static_cast<EdgeMode>(v);
	}

	LineMode getLineMode() const {
		int v = 0;

		_lineMode->get(v);

		return static_cast<LineMode>(v);
	}

	osg::Vec4 getColorBg() const { osg::Vec4 v; _colorBg->get(v); return v; }
	osg::Vec4 getColorLine() const { osg::Vec4 v; _colorLine->get(v); return v; }
	osg::Vec4 getColorLineStrong() const { osg::Vec4 v; _colorLineStrong->get(v); return v; }

	// --- Fullscreen overlay wiring -----------------------------------------------------------

	// Wraps `this` in a Geode, and that Geode in an ABSOLUTE_RF, PRE_RENDER, ortho2D(-1,1,-1,1)
	// camera -- the "always drawn first, in the background" fullscreen NDC setup this class
	// exists for. PRE_RENDER runs before the viewer's own NESTED_RENDER camera regardless of
	// scene graph position; the viewport is deliberately left unset so it inherits the window's
	// current viewport and tracks resizes for free.
	//
	// The caller still needs to drop the viewer's main camera to GL_DEPTH_BUFFER_BIT-only
	// clearing, or its default COLOR_BUFFER_BIT clear will stomp this camera's paint:
	//
	//   viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT);
	osg::ref_ptr<osg::Camera> orthoCamera() {
		auto geode = make_ref<osg::Geode>();

		geode->addDrawable(this);

		auto camera = make_ref<osg::Camera>();

		camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
		camera->setRenderOrder(osg::Camera::PRE_RENDER);
		camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		camera->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		camera->setProjectionMatrix(osg::Matrix::ortho2D(-1.0, 1.0, -1.0, 1.0));
		camera->setViewMatrix(osg::Matrix::identity());
		camera->setAllowEventFocus(false);
		camera->setCullingActive(false);

		camera->addChild(geode);

		return camera;
	}

	// Skips the throwaway named instance: constructs a Grid and immediately wraps it.
	// Keep concrete overloads for the public API so language bindings do not need
	// their own factory implementations.
	static osg::ref_ptr<osg::Camera> createOrthoCamera() {
		return make_ref<Grid>()->orthoCamera();
	}

	static osg::ref_ptr<osg::Camera> createOrthoCamera(
		const osg::Vec3& corner,
		const osg::Vec3& widthVec,
		const osg::Vec3& heightVec
	) {
		return make_ref<Grid>(corner, widthVec, heightVec)->orthoCamera();
	}

private:
	void _build(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec) {
		auto vertices = make_ref<osg::Vec3Array>();

		vertices->push_back(corner);
		vertices->push_back(corner + widthVec);
		vertices->push_back(corner + widthVec + heightVec);
		vertices->push_back(corner + heightVec);

		auto texCoords = make_ref<osg::Vec2Array>();

		texCoords->push_back(osg::Vec2(0.0f, 0.0f));
		texCoords->push_back(osg::Vec2(1.0f, 0.0f));
		texCoords->push_back(osg::Vec2(1.0f, 1.0f));
		texCoords->push_back(osg::Vec2(0.0f, 1.0f));

		auto normals = make_ref<osg::Vec3Array>();
		auto normal = widthVec ^ heightVec;

		normal.normalize();
		normals->push_back(normal);

		setVertexArray(vertices);
		setTexCoordArray(0, texCoords);
		setNormalArray(normals, osg::Array::BIND_OVERALL);
		// GL_QUADS is invalid in a core-profile context (including the 4.6 core
		// profile used by pyosg-lighting). A four-vertex triangle fan describes the
		// same rectangle without falling back to that removed legacy primitive.
		addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLE_FAN, 0, 4));

		_installState();
	}

	void _installState() {
		auto* ss = getOrCreateStateSet();
		auto program = make_ref<osg::Program>();

		program->addShader(new osg::Shader(osg::Shader::VERTEX, GRID_VERTEX_SHADER));
		program->addShader(new osg::Shader(osg::Shader::FRAGMENT, GRID_FRAGMENT_SHADER));

		ss->setAttributeAndModes(program, osg::StateAttribute::ON);
		ss->setMode(GL_BLEND, osg::StateAttribute::ON);
		// Enabling GL_BLEND alone leaves GL's default blend func (ONE, ZERO), which just
		// overwrites the destination regardless of alpha; SRC_ALPHA/ONE_MINUS_SRC_ALPHA is what
		// actually makes the zero-alpha background pixels transparent.
		ss->setAttributeAndModes(
			new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
			osg::StateAttribute::ON
		);

		_canvasSize = new osg::Uniform("u_canvasSize", osg::Vec2(300.0f, 300.0f));
		_gridInterval = new osg::Uniform("u_gridInterval", 5.0f);
		_gridIntervalStrong = new osg::Uniform("u_gridIntervalStrong", 10.0f);
		_lineWidthPx = new osg::Uniform("u_lineWidthPx", 1.0f);
		_lineWidth = new osg::Uniform("u_lineWidth", 0.5f);
		_edgeMode = new osg::Uniform("u_edgeMode", static_cast<int>(EDGE_ASIS));
		_lineMode = new osg::Uniform("u_lineMode", static_cast<int>(LINE_SCREEN_PIXELS));
		_colorBg = new osg::Uniform("u_colorBg", osg::Vec4(0.10f, 0.10f, 0.12f, 0.0f));
		_colorLine = new osg::Uniform("u_colorLine", osg::Vec4(0.45f, 0.45f, 0.50f, 1.0f));
		_colorLineStrong = new osg::Uniform("u_colorLineStrong", osg::Vec4(0.85f, 0.85f, 0.90f, 1.0f));

		ss->addUniform(_canvasSize);
		ss->addUniform(_gridInterval);
		ss->addUniform(_gridIntervalStrong);
		ss->addUniform(_lineWidthPx);
		ss->addUniform(_lineWidth);
		ss->addUniform(_edgeMode);
		ss->addUniform(_lineMode);
		ss->addUniform(_colorBg);
		ss->addUniform(_colorLine);
		ss->addUniform(_colorLineStrong);
	}

	// Re-locates uniform pointers from the (possibly newly-cloned) StateSet after a copy --
	// robust to both SHALLOW_COPY (StateSet shared, same uniform objects) and a deep-copy
	// CopyOp (StateSet cloned, uniforms cloned along with it).
	void _bindUniforms() {
		auto* ss = getOrCreateStateSet();

		_canvasSize = ss->getUniform("u_canvasSize");
		_gridInterval = ss->getUniform("u_gridInterval");
		_gridIntervalStrong = ss->getUniform("u_gridIntervalStrong");
		_lineWidthPx = ss->getUniform("u_lineWidthPx");
		_lineWidth = ss->getUniform("u_lineWidth");
		_edgeMode = ss->getUniform("u_edgeMode");
		_lineMode = ss->getUniform("u_lineMode");
		_colorBg = ss->getUniform("u_colorBg");
		_colorLine = ss->getUniform("u_colorLine");
		_colorLineStrong = ss->getUniform("u_colorLineStrong");
	}

	osg::ref_ptr<osg::Uniform> _canvasSize;
	osg::ref_ptr<osg::Uniform> _gridInterval;
	osg::ref_ptr<osg::Uniform> _gridIntervalStrong;
	osg::ref_ptr<osg::Uniform> _lineWidthPx;
	osg::ref_ptr<osg::Uniform> _lineWidth;
	osg::ref_ptr<osg::Uniform> _edgeMode;
	osg::ref_ptr<osg::Uniform> _lineMode;
	osg::ref_ptr<osg::Uniform> _colorBg;
	osg::ref_ptr<osg::Uniform> _colorLine;
	osg::ref_ptr<osg::Uniform> _colorLineStrong;
};


}
