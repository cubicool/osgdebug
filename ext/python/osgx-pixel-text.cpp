#include "osgx-python.hpp"
#include "osgx/PixelText.hpp"

#include <string>
#include <vector>

namespace osgx_python {

void bind_pixel_text(py::module_& m) {
	auto pixelText = py::class_<
		osgx::PixelText,
		osg::Geometry,
		osg::ref_ptr<osgx::PixelText>
	>(m, "PixelText");

	pixelText.attr("CHARSET") = std::string(osgx::PixelText::CHARSET);
	pixelText.attr("GLYPH_COLS") = osgx::PixelText::GLYPH_COLS;
	pixelText.attr("GLYPH_ROWS") = osgx::PixelText::GLYPH_ROWS;

	pixelText
		.def(py::init<>())
		.def(
			py::init<std::string_view, float>(),
			"text"_a,
			"cellSize"_a=1.0f,
			"In-scene text at any perspective/orientation - a gl_InstanceID-emitted quad per "
			"character, painted from a shared, process-wide atlas. Purely local-space geometry."
		)
		.def_static(
			"createAtlas",
			// pybind11's stl.h casts std::vector natively but has no std::span caster (it type-
			// erases the signature at bind time without actually failing until called - a real
			// TypeError at runtime, confirmed live). std::vector<std::string> -> std::span<const
			// std::string> converts implicitly on the way into the real function, so this stays
			// a thin bridge, not a reimplementation.
			[](
				const std::vector<std::string>& cells,
				int cellSize,
				int pixelScale,
				int multiPixelScale
			) {
				return osgx::PixelText::createAtlas(cells, cellSize, pixelScale, multiPixelScale);
			},
			"cells"_a,
			"cellSize"_a=64,
			"pixelScale"_a=7,
			"multiPixelScale"_a=5,
			"One atlas cell per entry in `cells`, each string centered within its own cell - for "
			"decal-style use (pyosg_dice.py calls this directly per die face)."
		)
		.def_property("text", &osgx::PixelText::getText, &osgx::PixelText::setText)
		.def_property("cellSize", &osgx::PixelText::getCellSize, &osgx::PixelText::setCellSize)
		.def_property("advance", &osgx::PixelText::getAdvance, &osgx::PixelText::setAdvance)
		.def_property("ink", &osgx::PixelText::getInk, &osgx::PixelText::setInk)
	;
}

}
