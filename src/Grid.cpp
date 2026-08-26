#include "osgx/Grid.hpp"
#include "osgx/Array.hpp"
#include "osgx/Shader.hpp"

#include <cmath>
#include <stdexcept>

namespace osgx {

namespace {

// Vertex shader hands the fragment shader a position in "grid space": UV (0..1) scaled by the
// logical canvas size. That keeps the grid's notion of "5 units apart" tied to the canvas the
// caller specified, independent of how big the quad actually is in NDC/world.
constexpr const char* GRID_VERTEX_SHADER = R"GLSL(
#version 430 core

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

constexpr const char* GRID_SHADER_LIBRARY = R"GLSL(

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

// Antialiased coverage (0..1) of gridlines spaced `interval` apart in `pos`'s units, with the
// line drawn `lineWidthPx` screen-pixels wide regardless of view distance/angle.
float osgx_GridLine(vec2 pos, float interval, float lineWidthPx) {
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
float osgx_PristineGridLine(vec2 pos, float interval, float lineWidth) {
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

vec4 osgx_GridColor(vec2 gridPos) {
	vec4 color = u_colorBg;

	float thin = u_lineMode == 0 ?
		osgx_GridLine(gridPos, u_gridInterval, u_lineWidthPx) :
		osgx_PristineGridLine(gridPos, u_gridInterval, u_lineWidth);

	color = mix(color, u_colorLine, thin);

	if(u_gridIntervalStrong > 0.0) {
		float strong = u_lineMode == 0 ?
			osgx_GridLine(gridPos, u_gridIntervalStrong, u_lineWidthPx * 1.5) :
			osgx_PristineGridLine(gridPos, u_gridIntervalStrong, u_lineWidth * 1.5);

		color = mix(color, u_colorLineStrong, strong);
	}

	return color;
}
)GLSL";

constexpr const char* GRID_FRAGMENT_SHADER = R"GLSL(
#version 430 core

#pragma osgx::grid GRID

in vec2 gridPos;

out vec4 fragColor;

void main(void) {
	fragColor = osgx_GridColor(gridPos);
}
)GLSL";

}

void registerGridShaderLibs() {
	static constexpr ShaderLib libs[] = {
		{"GRID", "osgx_GridColor", GRID_SHADER_LIBRARY}
	};

	::osgx::registerShaderLibs("osgx::grid", libs);
}

void Grid::_build(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec) {
	auto vertices = make_ref<Vec3Array>();

	vertices->push_back(corner);
	vertices->push_back(corner + widthVec);
	vertices->push_back(corner + widthVec + heightVec);
	vertices->push_back(corner + heightVec);

	auto texCoords = make_ref<Vec2Array>();

	texCoords->push_back(osg::Vec2(0.0f, 0.0f));
	texCoords->push_back(osg::Vec2(1.0f, 0.0f));
	texCoords->push_back(osg::Vec2(1.0f, 1.0f));
	texCoords->push_back(osg::Vec2(0.0f, 1.0f));

	auto normals = make_ref<Vec3Array>();
	auto normal = widthVec ^ heightVec;

	normal.normalize();
	normals->push_back(normal);

	setVertexArray(vertices);
	setTexCoordArray(0, texCoords);
	setNormalArray(normals, osg::Array::BIND_OVERALL);
	// GL_QUADS is invalid in a core-profile context (including the 4.6 core
	// profile used by pyosg-lighting). A four-vertex triangle fan describes the
	// same rectangle without falling back to that removed legacy primitive.
	addPrimitiveSet(new DrawArrays(osg::PrimitiveSet::TRIANGLE_FAN, 0, 4));

	_installState();
}

osg::ref_ptr<Grid> Grid::createSphere(float radius, unsigned int slices, unsigned int stacks) {
	auto grid = make_ref<Grid>();

	grid->_buildSphere(radius, slices, stacks);

	return grid;
}

void Grid::_buildSphere(float radius, unsigned int slices, unsigned int stacks) {
	if(radius <= 0.0f) throw std::invalid_argument("Grid sphere radius must be positive");
	if(slices < 3) throw std::invalid_argument("Grid sphere needs at least three slices");
	if(stacks < 2) throw std::invalid_argument("Grid sphere needs at least two stacks");

	auto vertices = make_ref<Vec3Array>();
	auto texCoords = make_ref<Vec2Array>();
	auto normals = make_ref<Vec3Array>();
	auto elements = make_ref<DrawElementsUInt>(osg::PrimitiveSet::TRIANGLES);

	vertices->reserve(static_cast<std::size_t>(slices + 1) * (stacks + 1));
	texCoords->reserve(vertices->capacity());
	normals->reserve(vertices->capacity());
	elements->reserve(static_cast<std::size_t>(slices) * stacks * 6);

	for(unsigned int stack = 0; stack <= stacks; stack++) {
		const auto v = static_cast<float>(stack) / static_cast<float>(stacks);
		const auto phi = v * static_cast<float>(osg::PI);
		const auto sinPhi = std::sin(phi);
		const auto cosPhi = std::cos(phi);

		for(unsigned int slice = 0; slice <= slices; slice++) {
			const auto u = static_cast<float>(slice) / static_cast<float>(slices);
			const auto theta = u * static_cast<float>(2.0 * osg::PI);
			const osg::Vec3 normal(
				sinPhi * std::cos(theta),
				sinPhi * std::sin(theta),
				cosPhi
			);

			vertices->push_back(normal * radius);
			texCoords->push_back(osg::Vec2(u, v));
			normals->push_back(normal);
		}
	}

	for(unsigned int stack = 0; stack < stacks; stack++) {
		for(unsigned int slice = 0; slice < slices; slice++) {
			const auto a = stack * (slices + 1) + slice;
			const auto b = a + 1;
			const auto c = a + slices + 1;
			const auto d = c + 1;

			elements->append(a, c, b, b, c, d);
		}
	}

	setVertexArray(vertices);
	setTexCoordArray(0, texCoords);
	setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
	removePrimitiveSet(0, getNumPrimitiveSets());
	addPrimitiveSet(elements);
	dirtyDisplayList();
	dirtyBound();
}

void Grid::_installState() {
	auto* ss = getOrCreateStateSet();
	auto program = make_ref<osg::Program>();

	registerGridShaderLibs();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, GRID_VERTEX_SHADER));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(GRID_FRAGMENT_SHADER)));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	configureStateSet(ss);
	_bindUniforms();
}

void Grid::configureStateSet(osg::StateSet* stateSet) {
	if(!stateSet) throw std::invalid_argument("Grid StateSet cannot be null");

	registerGridShaderLibs();

	stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
	// Enabling GL_BLEND alone leaves GL's default blend func (ONE, ZERO), which just
	// overwrites the destination regardless of alpha; SRC_ALPHA/ONE_MINUS_SRC_ALPHA is what
	// actually makes the zero-alpha background pixels transparent.
	stateSet->setAttributeAndModes(
		new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
		osg::StateAttribute::ON
	);

	stateSet->addUniform(new osg::Uniform("u_canvasSize", osg::Vec2(300.0f, 300.0f)));
	stateSet->addUniform(new osg::Uniform("u_gridInterval", 5.0f));
	stateSet->addUniform(new osg::Uniform("u_gridIntervalStrong", 10.0f));
	stateSet->addUniform(new osg::Uniform("u_lineWidthPx", 1.0f));
	stateSet->addUniform(new osg::Uniform("u_lineWidth", 0.5f));
	stateSet->addUniform(new osg::Uniform("u_edgeMode", static_cast<int>(EDGE_ASIS)));
	stateSet->addUniform(new osg::Uniform("u_lineMode", static_cast<int>(LINE_SCREEN_PIXELS)));
	stateSet->addUniform(new osg::Uniform("u_colorBg", osg::Vec4(0.10f, 0.10f, 0.12f, 0.0f)));
	stateSet->addUniform(new osg::Uniform("u_colorLine", osg::Vec4(0.45f, 0.45f, 0.50f, 1.0f)));
	stateSet->addUniform(new osg::Uniform("u_colorLineStrong", osg::Vec4(0.85f, 0.85f, 0.90f, 1.0f)));
}

void Grid::_bindUniforms() {
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

osg::ref_ptr<osg::Camera> Grid::orthoCamera() {
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

}
