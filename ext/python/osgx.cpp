// PYBIND11_MODULE entry point for the osgx Python module. Deliberately thin: every actual
// binding lives in its own osgx-*.cpp translation unit (see osgx-python.hpp for the bind_*
// declarations), each compiled into the osgx_python_bindings static library (CMakeLists.txt).
// This file just wires the submodules together, which keeps its own compile time (and therefore
// the cost of touching it) trivial, and means a change to one binding group no longer forces a
// full serial recompile of everything else.
#include "osgx-python.hpp"

PYBIND11_MODULE(osgx, m) {
	// Side effect, not the value: forces the OpenSceneGraph module's pybind11 types (osg::Geometry,
	// osgGA::CameraManipulator, etc.) to be registered before any osgx py::class_<> that derives
	// from one of them runs below.
	py::module_::import("OpenSceneGraph");

	osgx_python::bind_core(m);
	osgx_python::bind_callbacks(m);

	auto m_pbr = m.def_submodule("pbr", "osgx::pbr - BRDF math GLSL snippets + direct-light rig");

	osgx_python::bind_pbr(m_pbr);

	auto m_ibl = m.def_submodule(
		"ibl",
		"osgx::ibl - prefiltered cubemap + BRDF LUT + SH9/baked-Lambertian diffuse"
	);

	osgx_python::bind_ibl(m_ibl);

	auto m_debug = m.def_submodule("debug", "osgx::debug - GL_KHR_debug integration + GPU/CPU profiler");

	osgx_python::bind_debug(m_debug);

#ifdef OSGX_IMGUI
	auto m_imgui = m.def_submodule("imgui", "osgx::imgui namespace");

	osgx_python::bind_imgui(m_imgui);
#endif

	auto m_platform = m.def_submodule("platform", "osgx::platform - X11/EGL/GBM window helpers");

	osgx_python::bind_platform(m_platform);

	auto m_picking = m.def_submodule("picking", "osgx::picking - texture-based object-ID picking");

	osgx_python::bind_picking(m_picking);

	osgx_python::bind_shapes(m);

#ifdef OSGX_GLTF
	auto m_gltf = m.def_submodule("gltf", "osgx::gltf - glTF 2.0 loader + optional PBR/IBL adapter");

	osgx_python::bind_gltf(m_gltf);
#endif

	py::dict info;

	info["version"] = py::make_tuple(
		OSGX_VERSION_MAJOR,
		OSGX_VERSION_MINOR,
		OSGX_VERSION_PATCH
	);

	pyx::build_info(m, info);
}
