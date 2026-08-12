#include "osgx-python.hpp"
#include "osgx/Shapes.hpp"

namespace osgx_python {

void bind_shapes(py::module_& m) {
	py::class_<osgx::VertexLayout>(m, "VertexLayout")
		.def(py::init<>())
		.def_readwrite("position", &osgx::VertexLayout::position)
		.def_readwrite("normal", &osgx::VertexLayout::normal)
		.def_readwrite("uv", &osgx::VertexLayout::uv)
	;

	py::class_<
		osgx::Polyhedron,
		osg::Geometry,
		osg::ref_ptr<osgx::Polyhedron>
	> polyhedron(m, "Polyhedron");

	py::class_<osgx::Polyhedron::Face>(polyhedron, "Face")
		.def(py::init<>())
		.def(
			py::init<std::vector<std::uint32_t>, std::vector<osg::Vec2>>(),
			"vertices"_a,
			"uv"_a=std::vector<osg::Vec2>{}
		)
		.def_readwrite("vertices", &osgx::Polyhedron::Face::vertices)
		.def_readwrite("uv", &osgx::Polyhedron::Face::uv)
		.def("origin", &osgx::Polyhedron::Face::origin, "positions"_a)
		.def("center", &osgx::Polyhedron::Face::center, "positions"_a)
		.def("normal", &osgx::Polyhedron::Face::normal, "positions"_a)
		.def("right", &osgx::Polyhedron::Face::right, "positions"_a)
		.def("up", &osgx::Polyhedron::Face::up, "positions"_a)
		.def("planeCoordinates", &osgx::Polyhedron::Face::planeCoordinates, "positions"_a)
	;

	polyhedron
		.def(py::init<>())
		.def(
			py::init<
				std::vector<osg::Vec3>,
				std::vector<osgx::Polyhedron::Face>,
				osgx::VertexLayout
			>(),
			"vertices"_a,
			"faces"_a,
			"layout"_a=osgx::VertexLayout{}
		)
		.def_property(
			"vertices",
			[](const osgx::Polyhedron& self) { return self.vertices(); },
			&osgx::Polyhedron::setVertices
		)
		.def_property(
			"faces",
			[](const osgx::Polyhedron& self) { return self.faces(); },
			&osgx::Polyhedron::setFaces
		)
		.def_property("layout", &osgx::Polyhedron::layout, &osgx::Polyhedron::setLayout)
		.def("setFaceVertexAttribute", &osgx::Polyhedron::setFaceVertexAttribute)
		.def("setFaceAttribute", &osgx::Polyhedron::setFaceAttribute)
		.def("removeAttribute", &osgx::Polyhedron::removeAttribute)
		.def("rebuild", &osgx::Polyhedron::rebuild)
		.def("faceNormal", py::overload_cast<std::size_t>(&osgx::Polyhedron::faceNormal, py::const_))
		.def("faceUp", &osgx::Polyhedron::faceUp)
		.def("restingOffset", &osgx::Polyhedron::restingOffset, "orientation"_a=osg::Quat())
		.def(
			"faceRestingOffset",
			&osgx::Polyhedron::faceRestingOffset,
			"faceIndex"_a,
			"orientation"_a=osg::Quat()
		)
		.def_static("isometricFaceUV", &osgx::Polyhedron::isometricFaceUV)
	;

	py::class_<
		osgx::Cube,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::Cube>
	>(m, "Cube")
		.def(
			py::init<const osg::Vec3&, const osg::Vec3&, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(),
			"size"_a=osg::Vec3(1.0f, 1.0f, 1.0f),
			"layout"_a=osgx::VertexLayout{}
		)
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(),
			"radius"_a=1.0f,
			"layout"_a=osgx::VertexLayout{}
		)
	;

	py::class_<
		osgx::Tetrahedron,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::Tetrahedron>
	>(m, "Tetrahedron")
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(), "radius"_a=1.0f, "layout"_a=osgx::VertexLayout{}
		)
	;

	py::class_<
		osgx::Octahedron,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::Octahedron>
	>(m, "Octahedron")
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(), "radius"_a=1.0f, "layout"_a=osgx::VertexLayout{}
		)
	;

	py::class_<
		osgx::Icosahedron,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::Icosahedron>
	>(m, "Icosahedron")
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(), "radius"_a=1.0f, "layout"_a=osgx::VertexLayout{}
		)
	;

	py::class_<
		osgx::Dodecahedron,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::Dodecahedron>
	>(m, "Dodecahedron")
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(), "radius"_a=1.0f, "layout"_a=osgx::VertexLayout{}
		)
	;

	py::class_<
		osgx::PentagonalTrapezohedron,
		osgx::Polyhedron,
		osg::ref_ptr<osgx::PentagonalTrapezohedron>
	>(m, "PentagonalTrapezohedron")
		.def(
			py::init<const osg::Vec3&, float, osgx::VertexLayout>(),
			"center"_a=osg::Vec3(), "radius"_a=1.0f, "layout"_a=osgx::VertexLayout{}
		)
	;
}

}
