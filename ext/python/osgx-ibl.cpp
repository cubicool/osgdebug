#include "osgx-python.hpp"
#include "osgx/GGXPrefilter.hpp"
#include "osgx/IBL.hpp"
#include "osgx/LambertianBake.hpp"

namespace osgx_python {

void bind_ibl(py::module_& m_ibl) {
	osgx::registerIBLShaderLibs();

	m_ibl.attr("FULLSCREEN_VERT") = osgx::FULLSCREEN_VERT;
	m_ibl.attr("BRDF_LUT_FRAG") = osgx::BRDF_LUT_FRAG;
	m_ibl.attr("SH_IRRADIANCE") = osgx::SH_IRRADIANCE;
	m_ibl.attr("LAMBERTIAN_IRRADIANCE") = osgx::LAMBERTIAN_IRRADIANCE;

	py::class_<
		osgx::RunOnceCallback,
		osg::NodeCallback,
		osg::ref_ptr<osgx::RunOnceCallback>
	>(m_ibl, "RunOnceCallback")
		.def(py::init<>())
		.def("rebake", &osgx::RunOnceCallback::rebake, "node"_a)
	;

	m_ibl
		.def(
			"loadPrefilterCubemap",
			&osgx::loadPrefilterCubemap,
			"path"_a,
			"Loads a pre-baked GGX-prefiltered cubemap (.ktx2); returns None (and logs OSG_WARN) "
			"if the path doesn't load as a TextureCubeMap."
		)
		.def(
			"makeBRDFLUTCamera",
			&osgx::makeBRDFLUTCamera,
			"lutSize"_a,
			"lut"_a,
			"Configures `lut` in-place (size/format/filters) and returns a PRE_RENDER camera that "
			"bakes the split-sum BRDF LUT into it exactly once (see RunOnceCallback)."
		)
	;

	py::class_<osgx::SH9>(m_ibl, "SH9")
		.def(py::init<>())
		.def("__len__", [](const osgx::SH9&) { return 9; })
		.def("__getitem__", [](const osgx::SH9& self, size_t i) {
			if(i >= 9) throw py::index_error();

			return self.coeffs[i];
		})
		.def("__setitem__", [](osgx::SH9& self, size_t i, const osg::Vec3f& v) {
			if(i >= 9) throw py::index_error();

			self.coeffs[i] = v;
		})
	;

	m_ibl.def(
		"computeSH",
		&osgx::computeSH,
		"image"_a,
		"Projects an equirectangular HDR/LDR osg.Image onto SH9 diffuse irradiance coefficients."
	);

	m_ibl.def(
		"computeLambertianCubeMap",
		&osgx::computeLambertianCubeMap,
		"image"_a,
		"size"_a = 64,
		"samples"_a = 256,
		py::call_guard<py::gil_scoped_release>(),
		"Bakes a cosine-weighted Monte Carlo diffuse irradiance cubemap from an equirectangular "
		"HDR/LDR osg.Image -- more accurate than SH9 (see computeSH), at the cost of a real bake "
		"instead of 9 coefficients. Sample with LAMBERTIAN_IRRADIANCE's osgx_LambertianIrradiance()."
	);

	py::class_<osgx::GGXPrefilterOptions>(m_ibl, "GGXPrefilterOptions")
		.def(py::init<>())
		.def_readwrite("prefilterSize", &osgx::GGXPrefilterOptions::prefilterSize)
		.def_readwrite("sampleCount", &osgx::GGXPrefilterOptions::sampleCount)
		.def_readwrite("fireflyClamp", &osgx::GGXPrefilterOptions::fireflyClamp)
		.def_readwrite("maxFrames", &osgx::GGXPrefilterOptions::maxFrames)
		.def_readwrite("readbackFrame", &osgx::GGXPrefilterOptions::readbackFrame)
		.def_readwrite("syncReadback", &osgx::GGXPrefilterOptions::syncReadback)
	;

	py::class_<
		osgx::GGXPrefilterReadback,
		osg::Camera::DrawCallback,
		osg::ref_ptr<osgx::GGXPrefilterReadback>
	>(m_ibl, "GGXPrefilterReadback")
		.def_property_readonly("done", &osgx::GGXPrefilterReadback::isDone)
		.def_property_readonly("result", &osgx::GGXPrefilterReadback::getResult)
		.def("reset", &osgx::GGXPrefilterReadback::reset)
		.def("finish", &osgx::GGXPrefilterReadback::finish)
	;

	py::class_<osgx::GGXPrefilterScene>(m_ibl, "GGXPrefilterScene")
		.def_readonly("root", &osgx::GGXPrefilterScene::root)
		.def_readonly("sourceTexture", &osgx::GGXPrefilterScene::sourceTexture)
		.def_readonly("prefilterTexture", &osgx::GGXPrefilterScene::prefilterTexture)
		.def_readonly("readback", &osgx::GGXPrefilterScene::readback)
		.def_static(
			"create",
			&osgx::GGXPrefilterScene::create,
			"equirectImage"_a,
			"options"_a = osgx::GGXPrefilterOptions()
		)
		.def("rebake", &osgx::GGXPrefilterScene::rebake, "equirectImage"_a)
	;
}

}
