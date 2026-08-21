#include "osgx-python.hpp"
#include "osgx/GBuffer.hpp"

namespace osgx_python {

void bind_gbuffer(py::module_& m_gbuffer) {
	py::enum_<osgx::AttachmentFormat>(m_gbuffer, "AttachmentFormat")
		.value("RGBA8", osgx::AttachmentFormat::RGBA8)
		.value("RGB16F", osgx::AttachmentFormat::RGB16F)
		.value("RGBA16F", osgx::AttachmentFormat::RGBA16F)
		.value("RGBA32F", osgx::AttachmentFormat::RGBA32F)
	;

	py::class_<osgx::GBuffer>(m_gbuffer, "GBuffer")
		.def(py::init<>())
		.def_readwrite("camera", &osgx::GBuffer::camera)
		.def_readwrite("colorTextures", &osgx::GBuffer::colorTextures)
		.def_readwrite("depthTexture", &osgx::GBuffer::depthTexture)
		.def("valid", &osgx::GBuffer::valid)
		// GBuffer::create() takes a std::span, which pybind11 has no built-in caster for -- same
		// pattern osgx-core.cpp's findDataFile() binding uses: wrap in a lambda taking a
		// std::vector (pybind11/stl.h converts a Python list automatically) and build the span
		// from that inside the call.
		.def_static(
			"create",
			[](
				osg::Node* node,
				int width,
				int height,
				const std::vector<osgx::AttachmentFormat>& colorFormats,
				osg::Transform::ReferenceFrame referenceFrame
			) {
				return osgx::GBuffer::create(
					node, width, height, colorFormats, referenceFrame
				);
			},
			"node"_a,
			"width"_a,
			"height"_a,
			"colorFormats"_a,
			"referenceFrame"_a=osg::Transform::RELATIVE_RF,
			"Builds a PRE_RENDER FBO camera writing len(colorFormats) simultaneous color "
			"attachments (COLOR_BUFFER0..N) plus a real depth attachment, with `node` (a real 3D "
			"scene subgraph) as its child."
		)
	;
}

}
