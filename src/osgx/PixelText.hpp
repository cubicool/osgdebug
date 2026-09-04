#pragma once

#include "Array.hpp"
#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Array>
#include <osg/BoundingBox>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Uniform>

OSGX_ENABLE_WARNINGS

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osgx {

// ================================================================================================
// PixelText
//
// A deliberately small, deliberately inflexible procedural 5x9 bitmap font - the full printable
// ASCII set (upper+lowercase, digits, space, and punctuation), with a 2-row descender band below
// the baseline for lowercase g/j/p/q/y and a handful of punctuation marks (,;_|) - for quick
// in-scene labels and decal text until slughorn/osgSlug (the real text renderer) is wired in.
// Same spirit as osgx::imgui's
// Widget: one "bin" covering exactly the cases actually needed, not a flexible general-purpose
// text system - meant for fast prototyping and aipython-driven agentic development, a
// non-ugly placeholder for something better later, not a competitor to slughorn.
//
// Three proven entry points (see examples/osgx-pixel-text.cpp for all three):
//
//   - osgx::make_ref<PixelText>(text): in-scene text at any perspective/orientation - one
//     gl_InstanceID-emitted quad per character, painted from a shared, process-wide atlas.
//     Purely local-space geometry; compose with osg::AutoTransform/osg::Billboard or a plain
//     osg::MatrixTransform at the call site for camera-facing or screen-aligned placement -
//     nothing about PixelText itself assumes either. No PixelText::create() factory: the
//     constructor does all the real work itself, so a factory method would add nothing beyond
//     what make_ref<PixelText>(...) already gives you - create*() is reserved for the one entry
//     point below that actually does extra setup a plain constructor call can't.
//
//   - PixelText::createAtlas(cells): decal text baked onto a shape's faces (see
//     osgx::Polyhedron::setFaceAttribute()) - the same technique pyosg_dice.py's own
//     build_number_atlas() already uses per die face, generalized beyond digits.
//
//   - A screen-aligned HUD overlay needs no PixelText-specific API at all: wrap a
//     PixelText in a plain POST_RENDER, pixel-space-ortho osg::Camera at the call site (see
//     examples/osgx-pixel-text.cpp's makeHudLabel()) - HUD placement is exactly the kind of
//     camera-composition policy that stays at the call site, same reasoning as PixelText not
//     baking in camera-facing/billboard behavior itself.
//
// Everything else - the glyph table, the shared default atlas texture, the single-glyph-per-cell
// "whole charset" atlas createAtlas() is built from internally - stays private. Nobody's asked
// for that directly; createAtlas() is the one public "give me an atlas" entry point, for
// decal-style arbitrary cell lists.
// ================================================================================================

class PixelText: public osg::Geometry {
public:
	OSGX_META_Object(osgx, PixelText)

	// Every printable ASCII character this font can render (all 95 of them), in atlas-column
	// order. Case-sensitive - upper- and lowercase share no glyphs.
	static constexpr std::string_view CHARSET =
		" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
		"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
	;

	// One glyph is GLYPH_COLS wide x GLYPH_ROWS tall, in source (unscaled) pixels. Rows 0-6 are
	// the cap-height/baseline box (row 6 = baseline); rows 7-8 are the descender band, used only
	// by lowercase g/j/p/q/y and a few punctuation marks (,;_|) that dip below the baseline.
	static constexpr int GLYPH_COLS = 5;
	static constexpr int GLYPH_ROWS = 9;

	PixelText();
	explicit PixelText(std::string_view text, float cellSize=1.0f);
	PixelText(const PixelText& text, const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY);

	// A generalization of pyosg_dice.py's build_number_atlas(): one atlas cell per entry in
	// `cells`, each cell's (one-or-more-character) string centered as a single block within a
	// `cellSize` x `cellSize` GL_RED coverage cell. `pixelScale` sizes a single-character cell;
	// `multiPixelScale` (smaller, so a multi-character block still fits the same fixed cell
	// width) sizes any cell whose string is more than one character. Throws
	// std::invalid_argument if `cells` is empty or any entry contains a character outside
	// CHARSET (case-sensitive - upper- and lowercase are distinct glyphs).
	//
	// cellSize's default, (GLYPH_ROWS + 2) * pixelScale's default, leaves exactly one glyph-row
	// of margin above and below a full-height (GLYPH_ROWS * pixelScale tall) block - keep this in
	// sync if either default changes, or a full-height cell stops being centered with a visible
	// margin and instead touches the cell's top/bottom edge.
	//
	// verticallyCentered=true (the default) centers each cell's block against the ACTUAL ink rows
	// its own characters use (e.g. digits/uppercase only ever fill rows 0-6, never the rows 7-8
	// descender band), not the fixed GLYPH_ROWS - so a cell holding only characters that never
	// dip into the descender band renders identically to how the pre-descender-band 7-row font
	// centered it, without needing to know in advance whether a given cell's content has
	// descenders. Pass false for the old fixed-GLYPH_ROWS behavior, needed by the shared
	// whole-charset atlas PixelText itself renders from - see createCharsetAtlas() in
	// PixelText.cpp for why: every glyph there must sit on one shared baseline across the WHOLE
	// charset, not center independently per glyph, or a string mixing e.g. "A" and "q" would show
	// each letter floating to its own vertical middle instead of sharing a baseline.
	static osg::ref_ptr<osg::Image> createAtlas(
		std::span<const std::string> cells,
		int cellSize=77,
		int pixelScale=7,
		int multiPixelScale=5,
		bool verticallyCentered=true
	);

	// Characters outside CHARSET render as a blank space rather than throwing - a quick/dirty
	// label shouldn't reject a stray character. Case-sensitive - upper- and lowercase are
	// distinct glyphs.
	void setText(std::string_view text);
	const std::string& getText() const { return _text; }

	// World-space size of one glyph's quad. Affects the label's extent, so it dirties bounds.
	void setCellSize(float v) { _cellSize->set(v); dirtyBound(); }
	float getCellSize() const { float v = 0.0f; _cellSize->get(v); return v; }

	// Distance between successive glyph origins along local +X; defaults to cellSize, so a
	// caller only touches this to add letter-spacing. Affects the label's extent, so it dirties
	// bounds.
	void setAdvance(float v) { _advance->set(v); dirtyBound(); }
	float getAdvance() const { float v = 0.0f; _advance->get(v); return v; }

	void setInk(const osg::Vec4& v) { _ink->set(v); }
	osg::Vec4 getInk() const { osg::Vec4 v; _ink->get(v); return v; }

	// All per-glyph positioning happens in the vertex shader (see setText()'s comment in
	// PixelText.cpp), so OSG's normal CPU-side vertex-array bounds computation only ever sees
	// the single shared unit quad - without this override, a multi-character label's real
	// on-screen extent is silently understated and it gets culled/clipped as if it were one
	// glyph wide.
	osg::BoundingBox computeBoundingBox() const override;

private:
	void _build(float cellSize);
	void _installState();
	void _bindUniforms();

	// Lazily builds (once, process-wide) and returns the shared, GL_RED, LINEAR-filtered
	// texture every PixelText instance samples - see PixelText.cpp for why a static here is
	// safe (declared here with no body, defined exactly once in PixelText.cpp, same pattern
	// osgx::SharedBRDFLUT::create() and Shader.cpp's shader-lib registry already use).
	static osg::ref_ptr<osg::Texture2D> _atlasTexture();

	std::string _text;
	osg::ref_ptr<osg::DrawArrays> _instances;
	osg::ref_ptr<osg::Uniform> _cellSize;
	osg::ref_ptr<osg::Uniform> _advance;
	osg::ref_ptr<osg::Uniform> _ink;
};

}
