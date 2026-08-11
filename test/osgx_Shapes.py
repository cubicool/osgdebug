#vimrun! pytest -sv ../test/osgx_Shapes.py

import osgx

from OpenSceneGraph import *


def test_named_shapes_have_expected_topology_and_face_basis():
	# These counts are the unexpanded polyhedron data exposed to a dice layer.  Geometry itself
	# deliberately expands faces into flat-shaded triangle vertices during rebuild().
	expected = {
		osgx.Cube: (8, 6),
		osgx.Tetrahedron: (4, 4),
		osgx.Octahedron: (6, 8),
		osgx.Icosahedron: (12, 20),
		osgx.Dodecahedron: (20, 12),
		osgx.PentagonalTrapezohedron: (12, 10),
	}

	for kind, (vertex_count, face_count) in expected.items():
		shape = kind()

		assert len(shape.vertices) == vertex_count
		assert len(shape.faces) == face_count

		normal = shape.faceNormal(0)
		up = shape.faceUp(0)

		assert abs(normal.length() - 1.0) < 1e-5
		assert abs(up.length() - 1.0) < 1e-5
		assert abs(normal.dot(up)) < 1e-5
		assert shape.restingOffset() >= 0.0


def test_polyhedron_expands_face_attributes_and_removes_them():
	face = osgx.Face(
		[0, 1, 2],
		[osg.Vec2(0.0, 0.0), osg.Vec2(1.0, 0.0), osg.Vec2(0.0, 1.0)]
	)
	polyhedron = osgx.Polyhedron(
		[osg.Vec3(0.0, 0.0, 0.0), osg.Vec3(1.0, 0.0, 0.0), osg.Vec3(0.0, 1.0, 0.0)],
		[face]
	)

	# One value per source face becomes one value for every generated triangle vertex.
	polyhedron.setFaceAttribute(13, osg.Vec3Array([osg.Vec3(7.0, 8.0, 9.0)]))

	assert len(polyhedron.vertexAttrib[13]) == 3

	polyhedron.removeAttribute(13)

	assert 13 not in polyhedron.vertexAttrib
