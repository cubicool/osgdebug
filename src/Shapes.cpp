#include "osgx/Shapes.hpp"

#include "osgx/Array.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace osgx {

namespace {

using Face = Polyhedron::Face;

void copyElement(osg::Array* destination, std::size_t destinationIndex, const osg::Array* source, std::size_t sourceIndex) {
	if(destination->getElementSize() != source->getElementSize()) {
		throw std::logic_error("cannot copy between osg::Array types with different element sizes");
	}

	std::memcpy(
		const_cast<void*>(destination->getDataPointer(static_cast<unsigned int>(destinationIndex))),
		source->getDataPointer(static_cast<unsigned int>(sourceIndex)),
		destination->getElementSize()
	);
}

osg::ref_ptr<osg::Array> makeExpandedArray(const osg::Array* source, std::size_t size) {
	auto expanded = osg::ref_ptr<osg::Array>(dynamic_cast<osg::Array*>(source->cloneType()));

	if(!expanded) throw std::logic_error("osg::Array::cloneType() did not return an osg::Array");

	expanded->resizeArray(static_cast<unsigned int>(size));
	expanded->setBinding(osg::Array::BIND_PER_VERTEX);

	return expanded;
}

std::vector<osg::Vec3> cubeVertices(const osg::Vec3& center, const osg::Vec3& size) {
	const auto half = size * 0.5f;

	return {
		center + osg::Vec3(-half.x(), -half.y(), -half.z()),
		center + osg::Vec3( half.x(), -half.y(), -half.z()),
		center + osg::Vec3( half.x(), half.y(), -half.z()),
		center + osg::Vec3(-half.x(), half.y(), -half.z()),
		center + osg::Vec3(-half.x(), -half.y(), half.z()),
		center + osg::Vec3( half.x(), -half.y(), half.z()),
		center + osg::Vec3( half.x(), half.y(), half.z()),
		center + osg::Vec3(-half.x(), half.y(), half.z())
	};
}

osg::Vec3 cubeSizeForRadius(float radius) {
	const auto side = radius * 2.0f / std::sqrt(3.0f);

	return osg::Vec3(side, side, side);
}

std::vector<Face> cubeFaces() {
	const std::vector<osg::Vec2> uv = {
		osg::Vec2(0.0f, 0.0f), osg::Vec2(1.0f, 0.0f),
		osg::Vec2(1.0f, 1.0f), osg::Vec2(0.0f, 1.0f)
	};

	return {
		{{0, 3, 2, 1}, uv}, // -Z
		{{4, 5, 6, 7}, uv}, // +Z
		{{0, 1, 5, 4}, uv}, // -Y
		{{3, 7, 6, 2}, uv}, // +Y
		{{0, 4, 7, 3}, uv}, // -X
		{{1, 2, 6, 5}, uv} // +X
	};
}

std::vector<osg::Vec2> triangleUV() {
	return {
		osg::Vec2(0.0f, 0.0f),
		osg::Vec2(1.0f, 0.0f),
		osg::Vec2(0.5f, std::sqrt(3.0f) * 0.5f)
	};
}

std::vector<osg::Vec3> tetrahedronVertices(const osg::Vec3& center, float radius) {
	const auto scale = radius / std::sqrt(3.0f);

	return {
		center + osg::Vec3( scale, scale, scale),
		center + osg::Vec3(-scale, -scale, scale),
		center + osg::Vec3(-scale, scale, -scale),
		center + osg::Vec3( scale, -scale, -scale)
	};
}

std::vector<Face> tetrahedronFaces() {
	const auto uv = triangleUV();

	return {
		{{0, 2, 1}, uv},
		{{0, 1, 3}, uv},
		{{0, 3, 2}, uv},
		{{1, 2, 3}, uv}
	};
}

std::vector<osg::Vec3> octahedronVertices(const osg::Vec3& center, float radius) {
	return {
		center + osg::Vec3( radius, 0.0f, 0.0f),
		center + osg::Vec3(-radius, 0.0f, 0.0f),
		center + osg::Vec3(0.0f, radius, 0.0f),
		center + osg::Vec3(0.0f, -radius, 0.0f),
		center + osg::Vec3(0.0f, 0.0f, radius),
		center + osg::Vec3(0.0f, 0.0f, -radius)
	};
}

std::vector<Face> octahedronFaces() {
	const auto uv = triangleUV();

	return {
		{{0, 2, 4}, uv}, {{5, 2, 0}, uv}, {{4, 3, 0}, uv}, {{0, 3, 5}, uv},
		{{4, 2, 1}, uv}, {{1, 2, 5}, uv}, {{1, 3, 4}, uv}, {{5, 3, 1}, uv}
	};
}

std::vector<osg::Vec3> icosahedronVertices(const osg::Vec3& center, float radius) {
	const auto phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
	const auto scale = radius / std::sqrt(1.0f + phi * phi);

	return {
		center + osg::Vec3(-scale, phi * scale, 0.0f),
		center + osg::Vec3( scale, phi * scale, 0.0f),
		center + osg::Vec3(-scale, -phi * scale, 0.0f),
		center + osg::Vec3( scale, -phi * scale, 0.0f),
		center + osg::Vec3(0.0f, -scale, phi * scale),
		center + osg::Vec3(0.0f, scale, phi * scale),
		center + osg::Vec3(0.0f, -scale, -phi * scale),
		center + osg::Vec3(0.0f, scale, -phi * scale),
		center + osg::Vec3( phi * scale, 0.0f, -scale),
		center + osg::Vec3( phi * scale, 0.0f, scale),
		center + osg::Vec3(-phi * scale, 0.0f, -scale),
		center + osg::Vec3(-phi * scale, 0.0f, scale)
	};
}

std::vector<Face> icosahedronFaces() {
	const auto uv = triangleUV();

	return {
		{{0, 11, 5}, uv}, {{0, 5, 1}, uv}, {{0, 1, 7}, uv}, {{0, 7, 10}, uv}, {{0, 10, 11}, uv},
		{{1, 5, 9}, uv}, {{5, 11, 4}, uv}, {{11, 10, 2}, uv}, {{10, 7, 6}, uv}, {{7, 1, 8}, uv},
		{{3, 9, 4}, uv}, {{3, 4, 2}, uv}, {{3, 2, 6}, uv}, {{3, 6, 8}, uv}, {{3, 8, 9}, uv},
		{{4, 9, 5}, uv}, {{2, 4, 11}, uv}, {{6, 2, 10}, uv}, {{8, 6, 7}, uv}, {{9, 8, 1}, uv}
	};
}

std::vector<osg::Vec3> dodecahedronVertices(const osg::Vec3& center, float radius) {
	const auto phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
	const auto scale = radius / std::sqrt(3.0f);
	std::vector<osg::Vec3> vertices;

	vertices.reserve(20);

	for(const auto sx: {1.0f, -1.0f}) {
		for(const auto sy: {1.0f, -1.0f}) {
			for(const auto sz: {1.0f, -1.0f}) {
				vertices.push_back(center + osg::Vec3(sx * scale, sy * scale, sz * scale));
			}
		}
	}

	for(const auto s1: {1.0f, -1.0f}) {
		for(const auto s2: {1.0f, -1.0f}) {
			vertices.push_back(center + osg::Vec3(0.0f, s1 / phi * scale, s2 * phi * scale));
			vertices.push_back(center + osg::Vec3(s1 / phi * scale, s2 * phi * scale, 0.0f));
			vertices.push_back(center + osg::Vec3(s2 * phi * scale, 0.0f, s1 / phi * scale));
		}
	}

	return vertices;
}

std::vector<Face> dodecahedronFaces(const std::vector<osg::Vec3>& vertices) {
	std::vector<Face> faces = {
		{{9, 15, 4, 8, 0}, {}}, {{10, 16, 1, 9, 0}, {}}, {{8, 14, 2, 10, 0}, {}},
		{{11, 5, 15, 9, 1}, {}}, {{16, 3, 17, 11, 1}, {}}, {{12, 3, 16, 10, 2}, {}},
		{{14, 6, 18, 12, 2}, {}}, {{12, 18, 7, 17, 3}, {}}, {{13, 6, 14, 8, 4}, {}},
		{{15, 5, 19, 13, 4}, {}}, {{11, 17, 7, 19, 5}, {}}, {{13, 19, 7, 18, 6}, {}}
	};

	for(auto& face: faces) face.uv = Polyhedron::isometricFaceUV(vertices, face);

	return faces;
}

std::vector<osg::Vec3> pentagonalTrapezohedronVertices(const osg::Vec3& center, float radius) {
	constexpr float apexHeight = 1.35f;
	const auto c = std::cos(std::numbers::pi_v<float> / 5.0f);
	const auto ringZ = apexHeight * (1.0f - c) / (1.0f + c);
	const auto scale = radius / apexHeight;
	std::vector<osg::Vec3> vertices;

	vertices.reserve(12);

	for(std::size_t i = 0; i < 10; i++) {
		const auto angle = static_cast<float>(i) * std::numbers::pi_v<float> / 5.0f;
		const auto z = i % 2 == 0 ? ringZ : -ringZ;

		vertices.push_back(center + osg::Vec3(std::cos(angle) * scale, std::sin(angle) * scale, z * scale));
	}

	vertices.push_back(center + osg::Vec3(0.0f, 0.0f, radius));
	vertices.push_back(center + osg::Vec3(0.0f, 0.0f, -radius));

	return vertices;
}

std::vector<Face> pentagonalTrapezohedronFaces(const std::vector<osg::Vec3>& vertices) {
	std::vector<Face> faces;

	faces.reserve(10);

	for(std::uint32_t i = 0; i < 10; i++) {
		const auto previous = (i + 9) % 10;
		const auto next = (i + 1) % 10;
		const auto apex = i % 2 == 0 ? 11u : 10u;

		faces.push_back({{apex, i % 2 == 0 ? next : previous, i, i % 2 == 0 ? previous : next}, {}});
	}

	for(auto& face: faces) face.uv = Polyhedron::isometricFaceUV(vertices, face);

	return faces;
}

}

Polyhedron::Polyhedron(std::vector<osg::Vec3> vertices, std::vector<Face> faces, VertexLayout layout):
_vertices(std::move(vertices)),
_faces(std::move(faces)),
_layout(layout) {
	rebuild();
}

Polyhedron::Polyhedron(const Polyhedron& polyhedron, const osg::CopyOp& co):
osg::Geometry(polyhedron, co),
_vertices(polyhedron._vertices),
_faces(polyhedron._faces),
_layout(polyhedron._layout),
_attributes(polyhedron._attributes) {}

void Polyhedron::setVertices(std::vector<osg::Vec3> vertices) {
	_vertices = std::move(vertices);

	rebuild();
}

void Polyhedron::setFaces(std::vector<Face> faces) {
	_faces = std::move(faces);

	rebuild();
}

void Polyhedron::setLayout(VertexLayout layout) {
	if(_layout.position != layout.position) setVertexAttribArray(_layout.position, nullptr);
	if(_layout.normal != layout.normal) setVertexAttribArray(_layout.normal, nullptr);
	if(_layout.uv != layout.uv) setVertexAttribArray(_layout.uv, nullptr);

	_layout = layout;

	rebuild();
}

osg::Vec3 Polyhedron::faceNormal(std::size_t faceIndex) const {
	return _faces.at(faceIndex).normal(_vertices);
}

osg::Vec3 Polyhedron::faceUp(std::size_t faceIndex) const {
	return _faces.at(faceIndex).up(_vertices);
}

float Polyhedron::restingOffset(const osg::Quat& orientation) const {
	if(_vertices.empty()) return 0.0f;

	auto lowest = (orientation * _vertices.front()).z();

	for(const auto& vertex: _vertices) lowest = std::min(lowest, (orientation * vertex).z());

	return -lowest;
}

float Polyhedron::faceRestingOffset(std::size_t faceIndex, const osg::Quat& orientation) const {
	const auto& face = _faces.at(faceIndex);

	if(face.vertices.empty()) throw std::invalid_argument("Polyhedron support face has no vertices");

	float z = 0.0f;

	for(const auto index: face.vertices) z += (orientation * _vertices.at(index)).z();

	return -z / static_cast<float>(face.vertices.size());
}

void Polyhedron::setFaceVertexAttribute(unsigned int location, osg::Array* values) {
	setAttribute(location, AttributeDomain::FaceVertex, values);
}

void Polyhedron::setFaceAttribute(unsigned int location, osg::Array* values) {
	setAttribute(location, AttributeDomain::Face, values);
}

void Polyhedron::removeAttribute(unsigned int location) {
	const auto removed = std::erase_if(_attributes, [location](const auto& attribute) {
		return attribute.location == location;
	});

	if(removed != 0) setVertexAttribArray(location, nullptr);

	rebuild();
}

void Polyhedron::rebuild() {
	if(_layout.position == _layout.normal || _layout.position == _layout.uv || _layout.normal == _layout.uv) {
		throw std::invalid_argument("Polyhedron vertex attribute locations must be distinct");
	}

	std::vector<osg::Vec3> positions;
	std::vector<osg::Vec3> normals;
	std::vector<osg::Vec2> uvs;
	std::vector<osg::ref_ptr<osg::Array>> expandedAttributes;
	std::vector<std::size_t> faceVertexOffsets;
	std::size_t outputCount = 0;
	std::size_t faceVertexOffset = 0;

	faceVertexOffsets.reserve(_faces.size());

	for(const auto& face: _faces) {
		if(face.vertices.size() < 3) throw std::invalid_argument("Polyhedron faces need at least three vertices");
		if(!face.uv.empty() && face.uv.size() != face.vertices.size()) {
			throw std::invalid_argument("Polyhedron face UVs must be empty or match its vertex count");
		}

		for(const auto index: face.vertices) {
			if(index >= _vertices.size()) throw std::out_of_range("Polyhedron face refers to a missing vertex");
		}

		faceVertexOffsets.push_back(faceVertexOffset);
		faceVertexOffset += face.vertices.size();
		outputCount += (face.vertices.size() - 2) * 3;
	}

	for(const auto& attribute: _attributes) {
		const std::size_t expected = attribute.domain == AttributeDomain::Face
			? _faces.size()
			: faceVertexCount()
		;

		if(!attribute.values || attribute.values->getNumElements() != expected) {
			throw std::invalid_argument("Polyhedron attribute count does not match its declared domain");
		}

		if(
			attribute.location == _layout.position ||
			attribute.location == _layout.normal ||
			attribute.location == _layout.uv
		) {
			throw std::invalid_argument("Polyhedron custom attribute overlaps a built-in attribute location");
		}

		expandedAttributes.push_back(makeExpandedArray(attribute.values, outputCount));
	}

	positions.reserve(outputCount);
	normals.reserve(outputCount);

	const auto hasUV = !_faces.empty() && std::ranges::all_of(_faces, [](const auto& face) {
		return !face.uv.empty();
	});

	if(!hasUV && std::ranges::any_of(_faces, [](const auto& face) { return !face.uv.empty(); })) {
		throw std::invalid_argument("Polyhedron UVs must be supplied for every face or no faces");
	}

	if(hasUV) uvs.reserve(outputCount);

	for(std::size_t faceIndex = 0; faceIndex < _faces.size(); faceIndex++) {
		const auto& face = _faces[faceIndex];
		const auto normal = face.normal(_vertices);

		for(std::size_t triangle = 1; triangle + 1 < face.vertices.size(); triangle++) {
			for(const auto corner: {0_sz, triangle, triangle + 1}) {
				positions.push_back(_vertices[face.vertices[corner]]);
				normals.push_back(normal);

				if(hasUV) uvs.push_back(face.uv[corner]);

				const auto outputIndex = positions.size() - 1;

				for(std::size_t attributeIndex = 0; attributeIndex < _attributes.size(); attributeIndex++) {
					const auto& attribute = _attributes[attributeIndex];
					const auto sourceIndex = attribute.domain == AttributeDomain::Face
						? faceIndex
						: faceVertexOffsets[faceIndex] + corner
					;

					copyElement(expandedAttributes[attributeIndex], outputIndex, attribute.values, sourceIndex);
				}
			}
		}
	}

	removePrimitiveSet(0, getNumPrimitiveSets());

	auto positionArray = new osgx::Vec3Array(positions);
	auto normalArray = new osgx::Vec3Array(normals);

	positionArray->setBinding(osg::Array::BIND_PER_VERTEX);
	normalArray->setBinding(osg::Array::BIND_PER_VERTEX);

	setVertexArray(positionArray);
	setNormalArray(normalArray);
	setVertexAttribArray(_layout.position, positionArray);
	setVertexAttribArray(_layout.normal, normalArray);

	if(!uvs.empty()) {
		auto uvArray = new osgx::Vec2Array(uvs);

		uvArray->setBinding(osg::Array::BIND_PER_VERTEX);
		setVertexAttribArray(_layout.uv, uvArray);
	}

	else setVertexAttribArray(_layout.uv, nullptr);

	for(std::size_t i = 0; i < _attributes.size(); i++) {
		setVertexAttribArray(_attributes[i].location, expandedAttributes[i]);
	}

	addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(positions.size())));
	dirtyDisplayList();
	dirtyBound();
}

osg::Vec3 Polyhedron::Face::origin(const std::vector<osg::Vec3>& positions) const {
	return positions.at(vertices.at(0));
}

osg::Vec3 Polyhedron::Face::center(const std::vector<osg::Vec3>& positions) const {
	if(vertices.size() < 3) throw std::invalid_argument("Polyhedron faces need at least three vertices");

	const auto faceOrigin = origin(positions);
	const auto faceRight = right(positions);
	const auto faceUp = up(positions);
	const auto coordinates = planeCoordinates(positions);
	float twiceArea = 0.0f;
	float x = 0.0f;
	float y = 0.0f;

	for(std::size_t i = 0; i < coordinates.size(); i++) {
		const auto& a = coordinates[i];
		const auto& b = coordinates[(i + 1) % coordinates.size()];
		const auto cross = a.x() * b.y() - b.x() * a.y();

		twiceArea += cross;
		x += (a.x() + b.x()) * cross;
		y += (a.y() + b.y()) * cross;
	}

	if(twiceArea == 0.0f) throw std::invalid_argument("Polyhedron face has no area");

	return faceOrigin + faceRight * (x / (3.0f * twiceArea)) + faceUp * (y / (3.0f * twiceArea));
}

osg::Vec3 Polyhedron::Face::normal(const std::vector<osg::Vec3>& positions) const {
	if(vertices.size() < 3) throw std::invalid_argument("Polyhedron faces need at least three vertices");

	const auto& v0 = positions.at(vertices[0]);
	const auto& v1 = positions.at(vertices[1]);
	const auto& v2 = positions.at(vertices[2]);
	auto normal = (v1 - v0) ^ (v2 - v0);

	if(normal.length2() == 0.0f) throw std::invalid_argument("Polyhedron face has no normal");

	normal.normalize();

	return normal;
}

osg::Vec3 Polyhedron::Face::right(const std::vector<osg::Vec3>& positions) const {
	if(vertices.size() < 2) throw std::invalid_argument("Polyhedron faces need at least two vertices");

	auto right = positions.at(vertices[1]) - origin(positions);

	if(right.length2() == 0.0f) throw std::invalid_argument("Polyhedron face's first edge has no length");

	right.normalize();

	return right;
}

osg::Vec3 Polyhedron::Face::up(const std::vector<osg::Vec3>& positions) const {
	auto up = normal(positions) ^ right(positions);

	up.normalize();

	return up;
}

std::vector<osg::Vec2> Polyhedron::Face::planeCoordinates(const std::vector<osg::Vec3>& positions) const {
	const auto faceOrigin = origin(positions);
	const auto faceRight = right(positions);
	const auto faceUp = up(positions);
	std::vector<osg::Vec2> coordinates;

	coordinates.reserve(vertices.size());

	for(const auto index: vertices) {
		const auto offset = positions.at(index) - faceOrigin;

		coordinates.emplace_back(offset * faceRight, offset * faceUp);
	}

	return coordinates;
}

std::tuple<osg::Vec3, osg::Vec3, osg::Vec3, osg::Vec3> Polyhedron::Face::basis(
	const std::vector<osg::Vec3>& positions
) const {
	if(vertices.size() < 3) throw std::invalid_argument("Polyhedron faces need at least three vertices");

	const auto& faceOrigin = positions.at(vertices[0]);
	auto faceNormal = (positions.at(vertices[1]) - faceOrigin) ^ (positions.at(vertices[2]) - faceOrigin);

	if(faceNormal.length2() == 0.0f) throw std::invalid_argument("Polyhedron face has no normal");

	faceNormal.normalize();

	auto faceRight = positions.at(vertices[1]) - faceOrigin;

	if(faceRight.length2() == 0.0f) throw std::invalid_argument("Polyhedron face's first edge has no length");

	faceRight.normalize();

	auto faceUp = faceNormal ^ faceRight;

	faceUp.normalize();

	return {faceOrigin, faceNormal, faceRight, faceUp};
}

std::tuple<float, float, float, float> Polyhedron::Face::extent(const std::vector<osg::Vec3>& positions) const {
	osg::Vec3 faceOrigin, faceRight, faceUp;

	std::tie(faceOrigin, std::ignore, faceRight, faceUp) = basis(positions);

	float minU = 0.0f, maxU = 0.0f, minV = 0.0f, maxV = 0.0f;

	for(std::size_t i = 0; i < vertices.size(); i++) {
		const auto offset = positions.at(vertices[i]) - faceOrigin;
		const float u = offset * faceRight;
		const float v = offset * faceUp;

		if(i == 0) {
			minU = maxU = u;
			minV = maxV = v;
		} else {
			minU = std::min(minU, u); maxU = std::max(maxU, u);
			minV = std::min(minV, v); maxV = std::max(maxV, v);
		}
	}

	return {minU, maxU, minV, maxV};
}

osg::Vec3 Polyhedron::faceNormal(const std::vector<osg::Vec3>& vertices, const Face& face) {
	return face.normal(vertices);
}

std::vector<osg::Vec2> Polyhedron::isometricFaceUV(const std::vector<osg::Vec3>& vertices, const Face& face) {
	return face.planeCoordinates(vertices);
}

std::size_t Polyhedron::faceVertexCount() const {
	std::size_t count = 0;

	for(const auto& face: _faces) count += face.vertices.size();

	return count;
}

void Polyhedron::setAttribute(unsigned int location, AttributeDomain domain, osg::Array* values) {
	if(!values) throw std::invalid_argument("Polyhedron attributes cannot be null");

	auto found = std::ranges::find(_attributes, location, &Attribute::location);

	if(found == _attributes.end()) _attributes.push_back({location, domain, values});

	else *found = {location, domain, values};

	rebuild();
}

Cube::Cube(const osg::Vec3& center, const osg::Vec3& size, VertexLayout layout):
Polyhedron(cubeVertices(center, size), cubeFaces(), layout) {}

Cube::Cube(const osg::Vec3& center, float radius, VertexLayout layout):
Cube(center, cubeSizeForRadius(radius), layout) {}

Tetrahedron::Tetrahedron(const osg::Vec3& center, float radius, VertexLayout layout):
Polyhedron(tetrahedronVertices(center, radius), tetrahedronFaces(), layout) {}

Octahedron::Octahedron(const osg::Vec3& center, float radius, VertexLayout layout):
Polyhedron(octahedronVertices(center, radius), octahedronFaces(), layout) {}

Icosahedron::Icosahedron(const osg::Vec3& center, float radius, VertexLayout layout):
Polyhedron(icosahedronVertices(center, radius), icosahedronFaces(), layout) {}

Dodecahedron::Dodecahedron(const osg::Vec3& center, float radius, VertexLayout layout):
Polyhedron(
	dodecahedronVertices(center, radius),
	dodecahedronFaces(dodecahedronVertices(center, radius)),
	layout
) {}

PentagonalTrapezohedron::PentagonalTrapezohedron(const osg::Vec3& center, float radius, VertexLayout layout):
Polyhedron(
	pentagonalTrapezohedronVertices(center, radius),
	pentagonalTrapezohedronFaces(pentagonalTrapezohedronVertices(center, radius)),
	layout
) {}

}
