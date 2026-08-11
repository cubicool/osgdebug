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
//
// The GLSL vertex/fragment shader source (grid-space projection; gridLine()/pristineGridLine(),
// Ben Golus' "Pristine Grid" technique: https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8)
// lives entirely in Grid.cpp -- nothing outside this repo's own build has ever referenced those
// strings by name.
// ================================================================================================

class Grid: public osg::Geometry {
public:
	OSGX_META_Object(osgx, Grid)

	// Boundary-line handling for lines that fall exactly on the canvas edge (pos == 0 or
	// pos == canvasSize) -- see EDGE_ASIS/EDGE_HIDE/EDGE_NUDGE in the fragment shader (Grid.cpp)
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
	osg::ref_ptr<osg::Camera> orthoCamera();

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
	void _build(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec);
	void _installState();

	// Re-locates uniform pointers from the (possibly newly-cloned) StateSet after a copy --
	// robust to both SHALLOW_COPY (StateSet shared, same uniform objects) and a deep-copy
	// CopyOp (StateSet cloned, uniforms cloned along with it).
	void _bindUniforms();

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
