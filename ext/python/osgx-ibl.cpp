#include "osgx-python.hpp"

namespace osgx_python {

void bind_ibl(py::module_& m_ibl) {
	osgx::ibl::registerShaderLibs();

	m_ibl.attr("FULLSCREEN_VERT") = osgx::ibl::FULLSCREEN_VERT;
	m_ibl.attr("BRDF_LUT_FRAG") = osgx::ibl::BRDF_LUT_FRAG;
	m_ibl.attr("SH_IRRADIANCE") = osgx::ibl::SH_IRRADIANCE;
	m_ibl.attr("LAMBERTIAN_IRRADIANCE") = osgx::ibl::LAMBERTIAN_IRRADIANCE;

	py::class_<
		osgx::ibl::RunOnceCallback,
		osg::NodeCallback,
		osg::ref_ptr<osgx::ibl::RunOnceCallback>
	>(m_ibl, "RunOnceCallback")
		.def(py::init<>())
		.def("rebake", &osgx::ibl::RunOnceCallback::rebake, "node"_a)
	;

	m_ibl
		.def(
			"loadPrefilterCubemap",
			&osgx::ibl::loadPrefilterCubemap,
			"path"_a,
			"Loads a pre-baked GGX-prefiltered cubemap (.ktx2); returns None (and logs OSG_WARN) "
			"if the path doesn't load as a TextureCubeMap."
		)
		.def(
			"makeBRDFLUTCamera",
			&osgx::ibl::makeBRDFLUTCamera,
			"lutSize"_a,
			"lut"_a,
			"Configures `lut` in-place (size/format/filters) and returns a PRE_RENDER camera that "
			"bakes the split-sum BRDF LUT into it exactly once (see RunOnceCallback)."
		)
	;

	py::class_<osgx::ibl::SH9>(m_ibl, "SH9")
		.def(py::init<>())
		.def("__len__", [](const osgx::ibl::SH9&) { return 9; })
		.def("__getitem__", [](const osgx::ibl::SH9& self, size_t i) {
			if(i >= 9) throw py::index_error();

			return self.coeffs[i];
		})
		.def("__setitem__", [](osgx::ibl::SH9& self, size_t i, const osg::Vec3f& v) {
			if(i >= 9) throw py::index_error();

			self.coeffs[i] = v;
		})
	;

	m_ibl.def(
		"computeSH",
		&osgx::ibl::computeSH,
		"image"_a,
		"Projects an equirectangular HDR/LDR osg.Image onto SH9 diffuse irradiance coefficients."
	);

	m_ibl.def(
		"computeLambertianCubeMap",
		&osgx::ibl::computeLambertianCubeMap,
		"image"_a,
		"size"_a = 64,
		"samples"_a = 256,
		py::call_guard<py::gil_scoped_release>(),
		"Bakes a cosine-weighted Monte Carlo diffuse irradiance cubemap from an equirectangular "
		"HDR/LDR osg.Image -- more accurate than SH9 (see computeSH), at the cost of a real bake "
		"instead of 9 coefficients. Sample with LAMBERTIAN_IRRADIANCE's osgx_LambertianIrradiance()."
	);

	py::class_<osgx::ibl::GGXPrefilterOptions>(m_ibl, "GGXPrefilterOptions")
		.def(py::init<>())
		.def_readwrite("prefilterSize", &osgx::ibl::GGXPrefilterOptions::prefilterSize)
		.def_readwrite("sampleCount", &osgx::ibl::GGXPrefilterOptions::sampleCount)
		.def_readwrite("fireflyClamp", &osgx::ibl::GGXPrefilterOptions::fireflyClamp)
		.def_readwrite("maxFrames", &osgx::ibl::GGXPrefilterOptions::maxFrames)
		.def_readwrite("readbackFrame", &osgx::ibl::GGXPrefilterOptions::readbackFrame)
		.def_readwrite("syncReadback", &osgx::ibl::GGXPrefilterOptions::syncReadback)
	;

	py::class_<
		osgx::ibl::GGXPrefilterReadback,
		osg::Camera::DrawCallback,
		osg::ref_ptr<osgx::ibl::GGXPrefilterReadback>
	>(m_ibl, "GGXPrefilterReadback")
		.def_property_readonly("done", &osgx::ibl::GGXPrefilterReadback::isDone)
		.def_property_readonly("result", &osgx::ibl::GGXPrefilterReadback::getResult)
		.def("reset", &osgx::ibl::GGXPrefilterReadback::reset)
	;

	py::class_<osgx::ibl::GGXPrefilterScene>(m_ibl, "GGXPrefilterScene")
		.def_readonly("root", &osgx::ibl::GGXPrefilterScene::root)
		.def_readonly("sourceTexture", &osgx::ibl::GGXPrefilterScene::sourceTexture)
		.def_readonly("prefilterTexture", &osgx::ibl::GGXPrefilterScene::prefilterTexture)
		.def_readonly("readback", &osgx::ibl::GGXPrefilterScene::readback)
	;

	m_ibl
		.def(
			"createGGXPrefilterScene",
			&osgx::ibl::createGGXPrefilterScene,
			"equirectImage"_a,
			"options"_a = osgx::ibl::GGXPrefilterOptions()
		)
		.def(
			"rebakeGGXPrefilterScene",
			&osgx::ibl::rebakeGGXPrefilterScene,
			"scene"_a,
			"equirectImage"_a
		)
		.def("finishGGXPrefilter", &osgx::ibl::finishGGXPrefilter, "readback"_a)
	;
}

}
