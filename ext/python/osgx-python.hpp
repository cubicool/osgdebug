#pragma once

// Shared umbrella header for the split osgx Python bindings (see osgx.cpp for the
// PYBIND11_MODULE entry point). Deliberately does NOT include tinygltf/osgx::gltf headers
// (osgx-gltf.cpp owns those locally -- they're heavy and only that one file needs them) or the
// EGL/GBM window headers (osgx-platform.cpp owns those locally, same reasoning). Splitting was a
// straight build-time win: previously any change anywhere in the single ~1700-line
// ext/osgx-python.cpp forced a full serial recompile; now each bind_*() lives in its own
// translation unit, so touching one doesn't force the others to recompile, and a from-scratch
// build parallelizes across `make -j#`.

#include "osgx/osgx.hpp"
#include "osgx/Cursor.hpp"
#include "osgx/Debug.hpp"
#include "osgx/ImGui.hpp"
#include "osgx/Linux.hpp"
#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/functional.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace pyx = pybind11x;

namespace osgx_python {

void bind_core(py::module_& m);
void bind_callbacks(py::module_& m);
void bind_pbr(py::module_& m_pbr);
void bind_ibl(py::module_& m_ibl);
void bind_debug(py::module_& m_debug);
void bind_platform(py::module_& m_platform);
void bind_picking(py::module_& m_picking);

#ifdef OSGX_IMGUI
void bind_imgui(py::module_& m_imgui);
#endif

#ifdef OSGX_GLTF
void bind_gltf(py::module_& m_gltf);
#endif

}
