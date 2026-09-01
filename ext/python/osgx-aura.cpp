#include "osgx-python.hpp"
#include "osgx/Aura.hpp"

namespace osgx_python {

void bind_aura(py::module_& m) {
	py::class_<osgx::Aura>(
		m,
		"Aura",
		"A screen-space silhouette expansion pipeline: selectionCamera renders `selected` into "
		"originalMask, then two fullscreen passes separable-max-filter it into `expanded` (mask "
		"in .r, nearest selected-source UV in .gb, Chebyshev distance in pixels in .a). Build via "
		"Aura.create() -- the plain constructor leaves every camera/texture field empty."
	)
		.def(py::init<>(), "Constructs an empty Aura with no cameras/textures set; see Aura.create().")
		.def_readwrite(
			"selectionCamera", &osgx::Aura::selectionCamera,
			"Renders `selected` into originalMask; the first pass in the pipeline (PRE_RENDER order 1)."
		)
		.def_readwrite(
			"dilateXCamera", &osgx::Aura::dilateXCamera,
			"First separable max-filter pass: reads originalMask, writes dilatedX (PRE_RENDER order 2)."
		)
		.def_readwrite(
			"dilateYCamera", &osgx::Aura::dilateYCamera,
			"Second separable max-filter pass: reads dilatedX, writes expanded (PRE_RENDER order 3)."
		)
		.def_readwrite(
			"originalMask", &osgx::Aura::originalMask,
			"The selection camera's raw output: nonzero where `selected` was rendered, zero elsewhere."
		)
		.def_readwrite(
			"dilatedX", &osgx::Aura::dilatedX,
			"Intermediate result of the horizontal dilation pass, consumed by dilateYCamera."
		)
		.def_readwrite(
			"expanded", &osgx::Aura::expanded,
			"Final dilated result: mask in .r, nearest selected-source UV in .gb, Chebyshev "
			"distance in pixels in .a."
		)
		.def_readwrite(
			"radius", &osgx::Aura::radius,
			"Dilation radius in output pixels; both dilation shaders clamp it to their fixed "
			"maximum of 64."
		)
		.def(
			"valid", &osgx::Aura::valid,
			"True if every camera and texture in the pipeline was successfully built."
		)
		.def_static(
			"create",
			&osgx::Aura::create,
			"selected"_a,
			"width"_a,
			"height"_a,
			"radius"_a=8,
			"Builds a screen-space silhouette expansion pipeline: selectionCamera renders `selected` "
			"into originalMask, then two fullscreen passes separable-max-filter it into `expanded` "
			"(mask in .r, nearest selected-source UV in .gb, Chebyshev distance in pixels in .a). Add "
			"the three cameras to the render graph in the listed order (PRE_RENDER order 1, 2, 3 -- "
			"order 0 is left free for a preceding G-buffer pass). `selected` can be multi-parented "
			"normally (once under the visible scene, once under selectionCamera)."
		)
	;
}

}
