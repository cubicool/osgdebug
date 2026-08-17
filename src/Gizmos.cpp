#include "osgx/Gizmos.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/BoundingSphere>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Math>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Vec2>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <cmath>

namespace osgx::gizmo {

namespace detail {

constexpr int CIRCLE_SEGMENTS = 24;
constexpr int SPOT_RING_SEGMENTS = 24;
constexpr int SPOT_SPOKES = 8;
// Per-light vertex budget: whichever shape needs more (three wireframe circles for a point/sphere
// marker) sets the fixed per-slot capacity every light gets, so the backing arrays never change
// SIZE across a rebuild -- only their contents -- letting OSG upload via glBufferSubData instead
// of reallocating each frame (same lesson 11-sketchfab-lambertian.py's LightGizmoCallback comment
// records: a new Array object/size every frame forces a full glBufferData reallocation).
constexpr int MAX_VERTS_PER_LIGHT = 3 * CIRCLE_SEGMENTS;

constexpr const char GIZMO_VERTEX_SHADER[] = R"GLSL(
#version 460 core

in vec4 osgx_gizmo_Vertex;
in vec3 osgx_gizmo_Color;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec3 vColor;

void main() {
	vColor = osgx_gizmo_Color;
	gl_Position = osg_ModelViewProjectionMatrix * osgx_gizmo_Vertex;
}
)GLSL";

constexpr const char GIZMO_FRAGMENT_SHADER[] = R"GLSL(
#version 460 core

in vec3 vColor;

out vec4 fragColor;

void main() {
	fragColor = vec4(vColor, 1.0);
}
)GLSL";

osg::ref_ptr<osg::Program> createGizmoProgram() {
	auto program = new osg::Program();

	program->setName("osgx_gizmo");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, GIZMO_VERTEX_SHADER));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, GIZMO_FRAGMENT_SHADER));
	program->addBindAttribLocation("osgx_gizmo_Vertex", 0);
	program->addBindAttribLocation("osgx_gizmo_Color", 1);

	return program;
}

// Normalizes by the largest channel, matching 11-sketchfab-lambertian.py's LightGizmoCallback --
// a gizmo drawn at a light's raw HDR color (routinely >> 1.0) would just be solid white.
osg::Vec3 gizmoColor(const osg::Vec3& color) {
	float m = std::max({color.x(), color.y(), color.z(), 1e-4f});

	return color / m;
}

// Builds an orthonormal basis (u, v) perpendicular to `n`, same construction as
// 11-sketchfab-lambertian.py's LightGizmoCallback: a Z-up reference vector, except when `n` is
// nearly parallel to it, where X is used instead to avoid a degenerate/zero-length cross.
void perpendicularBasis(const osg::Vec3& n, osg::Vec3& u, osg::Vec3& v) {
	osg::Vec3 upRef = std::abs(n.z()) > 0.99f ? osg::Vec3(1.0f, 0.0f, 0.0f) : osg::Vec3(0.0f, 0.0f, 1.0f);

	u = upRef ^ n;
	u.normalize();
	v = n ^ u;
}

// Appends a wireframe circle (CIRCLE_SEGMENTS or SPOT_RING_SEGMENTS vertices, drawn as a
// LINE_LOOP primitive set covering [verts->size(), verts->size()+segments)) centered at `center`
// in the plane spanned by `u`/`v`.
void appendCircle(
	osg::Vec3Array* verts,
	osg::Vec3Array* colors,
	osg::Geometry* geom,
	std::size_t start,
	int segments,
	const osg::Vec3& center,
	const osg::Vec3& u,
	const osg::Vec3& v,
	float radius,
	const osg::Vec3& color
) {
	for(int i = 0; i < segments; i++) {
		float a = 2.0f * osg::PIf * static_cast<float>(i) / static_cast<float>(segments);

		(*verts)[start + static_cast<std::size_t>(i)] = center + u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
		(*colors)[start + static_cast<std::size_t>(i)] = color;
	}

	geom->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, static_cast<GLint>(start), segments));
}

// Point/sphere marker: three orthogonal wireframe circles (XY/XZ/YZ great circles) at `position`,
// sized to `radius`. Consumes exactly 3*CIRCLE_SEGMENTS vertices starting at `start`.
void buildPointMarker(
	osg::Vec3Array* verts,
	osg::Vec3Array* colors,
	osg::Geometry* geom,
	std::size_t start,
	const osg::Vec3& position,
	float radius,
	const osg::Vec3& color
) {
	appendCircle(
		verts, colors, geom, start, CIRCLE_SEGMENTS,
		position, osg::Vec3(1.0f, 0.0f, 0.0f), osg::Vec3(0.0f, 1.0f, 0.0f), radius, color
	);
	appendCircle(
		verts, colors, geom, start + CIRCLE_SEGMENTS, CIRCLE_SEGMENTS,
		position, osg::Vec3(1.0f, 0.0f, 0.0f), osg::Vec3(0.0f, 0.0f, 1.0f), radius, color
	);
	appendCircle(
		verts, colors, geom, start + 2 * CIRCLE_SEGMENTS, CIRCLE_SEGMENTS,
		position, osg::Vec3(0.0f, 1.0f, 0.0f), osg::Vec3(0.0f, 0.0f, 1.0f), radius, color
	);
}

// Spot marker: a wireframe cone -- a ring at `coneLength` along `direction` (radius from the
// outer cone half-angle) plus SPOT_SPOKES lines from the apex to that ring. Consumes at most
// MAX_VERTS_PER_LIGHT vertices starting at `start` (SPOT_RING_SEGMENTS + SPOT_SPOKES*2).
void buildSpotMarker(
	osg::Vec3Array* verts,
	osg::Vec3Array* colors,
	osg::Geometry* geom,
	std::size_t start,
	const osg::Vec3& apex,
	const osg::Vec3& direction,
	float outerConeAngle,
	float coneLength,
	const osg::Vec3& color
) {
	osg::Vec3 axis = direction;

	axis.normalize();

	osg::Vec3 u, v;

	perpendicularBasis(axis, u, v);

	osg::Vec3 ringCenter = apex + axis * coneLength;
	float ringRadius = coneLength * std::tan(outerConeAngle);

	appendCircle(verts, colors, geom, start, SPOT_RING_SEGMENTS, ringCenter, u, v, ringRadius, color);

	std::size_t spokeStart = start + static_cast<std::size_t>(SPOT_RING_SEGMENTS);
	int stride = std::max(1, SPOT_RING_SEGMENTS / SPOT_SPOKES);

	for(int i = 0; i < SPOT_SPOKES; i++) {
		float a = 2.0f * osg::PIf * static_cast<float>(i * stride) / static_cast<float>(SPOT_RING_SEGMENTS);
		osg::Vec3 ringPoint = ringCenter + u * (std::cos(a) * ringRadius) + v * (std::sin(a) * ringRadius);
		std::size_t base = spokeStart + static_cast<std::size_t>(i) * 2;

		(*verts)[base] = apex;
		(*colors)[base] = color;
		(*verts)[base + 1] = ringPoint;
		(*colors)[base + 1] = color;
	}

	geom->addPrimitiveSet(new osg::DrawArrays(GL_LINES, static_cast<GLint>(spokeStart), SPOT_SPOKES * 2));
}

}

LightMarkers::LightMarkers(const osgx::pbr::LightSet& lights, float minMarkerRadius, float spotConeLength) {
	setUpdateCallback(new UpdateCallback(lights, minMarkerRadius, spotConeLength));
	setCullingActive(false);
}

void LightMarkers::UpdateCallback::operator()(osg::Node* node, osg::NodeVisitor* nv) {
	if(auto* markers = dynamic_cast<LightMarkers*>(node); markers && _lights.valid())
		markers->rebuild(_lights, _minMarkerRadius, _spotConeLength);

	traverse(node, nv);
}

void LightMarkers::rebuild(const osgx::pbr::LightSet& lights, float minMarkerRadius, float spotConeLength) {
	if(!lights.valid()) return;

	if(!_geometry) {
		auto capacity = static_cast<unsigned int>(osgx::pbr::MAX_LIGHTS * detail::MAX_VERTS_PER_LIGHT);
		auto verts = osgx::make_ref<osg::Vec3Array>(capacity);
		auto colors = osgx::make_ref<osg::Vec3Array>(capacity);

		verts->setBinding(osg::Array::BIND_PER_VERTEX);
		colors->setBinding(osg::Array::BIND_PER_VERTEX);
		verts->setDataVariance(osg::Object::DYNAMIC);
		colors->setDataVariance(osg::Object::DYNAMIC);

		_geometry = osgx::make_ref<osg::Geometry>();
		_geometry->setUseVertexBufferObjects(true);
		_geometry->setDataVariance(osg::Object::DYNAMIC);
		_geometry->setVertexArray(verts.get());
		_geometry->setVertexAttribArray(0, verts.get());
		_geometry->setVertexAttribArray(1, colors.get());

		auto* ss2 = _geometry->getOrCreateStateSet();

		ss2->setAttributeAndModes(detail::createGizmoProgram().get(), osg::StateAttribute::ON);
		ss2->setAttributeAndModes(new osg::LineWidth(2.0f), osg::StateAttribute::ON);

		auto geode = osgx::make_ref<osg::Geode>();

		geode->addDrawable(_geometry.get());
		addChild(geode.get());
	}

	int count = std::clamp(lights.getCount(), 0, osgx::pbr::MAX_LIGHTS);

	_geometry->removePrimitiveSet(0, _geometry->getNumPrimitiveSets());

	auto* verts = static_cast<osg::Vec3Array*>(_geometry->getVertexArray());
	auto* colors = static_cast<osg::Vec3Array*>(_geometry->getVertexAttribArray(1));

	for(int i = 0; i < count; i++) {
		auto index = static_cast<std::size_t>(i);
		osgx::pbr::LightType type = lights.getType(index);
		osg::Vec4 posIntensity = lights.getPosIntensity(index);
		osg::Vec3 color = lights.getColor(index);
		float sourceRadius = lights.getSourceRadius(index);

		std::size_t slotStart = static_cast<std::size_t>(i) * static_cast<std::size_t>(detail::MAX_VERTS_PER_LIGHT);
		osg::Vec3 c = detail::gizmoColor(color);
		osg::Vec3 position(posIntensity.x(), posIntensity.y(), posIntensity.z());

		if(type == osgx::pbr::LightType::Spot) {
			osg::Vec3 direction = lights.getDirection(index);
			osg::Vec2 coneAngles = lights.getSpotAngles(index);
			float outerAngle = std::acos(std::clamp(coneAngles.y(), -1.0f, 1.0f));

			detail::buildSpotMarker(
				verts, colors, _geometry.get(), slotStart,
				position, direction, outerAngle, spotConeLength, c
			);
		}

		else if(type != osgx::pbr::LightType::Directional) {
			float radius = std::max(sourceRadius, minMarkerRadius);

			detail::buildPointMarker(verts, colors, _geometry.get(), slotStart, position, radius, c);
		}
	}

	verts->dirty();
	colors->dirty();
	_geometry->dirtyBound();
}

osg::ref_ptr<osg::Camera> createDirectionalOverlay(const osgx::pbr::LightSet& lights, osg::Node* scene) {
	osg::BoundingSphere bound = scene ? scene->getBound() : osg::BoundingSphere(osg::Vec3(), 1.0f);
	float boundRadius = bound.radius() > 0.0f ? bound.radius() : 1.0f;
	osg::Vec3 boundCenter = bound.center();

	// Proportional to the scene, same reasoning as 11-sketchfab-lambertian.py's
	// create_light_gizmo(): large enough to read as "an infinite plane of parallel rays" rather
	// than a small marker.
	float planeHalfSize = boundRadius * 2.5f;
	float normalLength = boundRadius * 0.8f;
	float arrowBack = normalLength * 0.2f;
	float arrowWidth = normalLength * 0.15f;

	// 10 vertices per directional slot: 4 (plane LINE_LOOP) + 2 (direction stub, LINES) + 4
	// (arrowhead, two LINES segments) -- exactly mirrors the Python original's per-light layout.
	constexpr int VERTS_PER_LIGHT = 10;
	auto capacity = static_cast<unsigned int>(osgx::pbr::MAX_LIGHTS * VERTS_PER_LIGHT);
	auto verts = osgx::make_ref<osg::Vec3Array>(capacity);
	auto colors = osgx::make_ref<osg::Vec3Array>(capacity);

	verts->setBinding(osg::Array::BIND_PER_VERTEX);
	colors->setBinding(osg::Array::BIND_PER_VERTEX);
	verts->setDataVariance(osg::Object::DYNAMIC);
	colors->setDataVariance(osg::Object::DYNAMIC);

	auto geom = osgx::make_ref<osg::Geometry>();

	geom->setUseVertexBufferObjects(true);
	geom->setDataVariance(osg::Object::DYNAMIC);
	geom->setVertexArray(verts.get());
	geom->setVertexAttribArray(0, verts.get());
	geom->setVertexAttribArray(1, colors.get());
	geom->setCullingActive(false);

	auto* geomSS = geom->getOrCreateStateSet();

	geomSS->setAttributeAndModes(detail::createGizmoProgram().get(), osg::StateAttribute::ON);
	geomSS->setAttributeAndModes(new osg::LineWidth(2.0f), osg::StateAttribute::ON);
	geomSS->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	geomSS->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(geom.get());

	struct DirectionalOverlayCallback: public osg::NodeCallback {
		DirectionalOverlayCallback(const osgx::pbr::LightSet& lights, osg::Vec3 center, float planeHalf, float normalLen, float back, float wide):
		_lights(lights), _center(center), _planeHalf(planeHalf), _normalLen(normalLen), _arrowBack(back), _arrowWidth(wide) {}

		void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
			auto* geode2 = dynamic_cast<osg::Geode*>(node);

			if(!geode2 || geode2->getNumDrawables() == 0 || !_lights.valid()) {
				traverse(node, nv);

				return;
			}

			auto* geom2 = static_cast<osg::Geometry*>(geode2->getDrawable(0));
			auto* verts2 = static_cast<osg::Vec3Array*>(geom2->getVertexArray());
			auto* colors2 = static_cast<osg::Vec3Array*>(geom2->getVertexAttribArray(1));

			geom2->removePrimitiveSet(0, geom2->getNumPrimitiveSets());

			int count = std::clamp(_lights.getCount(), 0, osgx::pbr::MAX_LIGHTS);

			for(int i = 0; i < count; i++) {
				auto index = static_cast<std::size_t>(i);
				osgx::pbr::LightType type = _lights.getType(index);

				if(type != osgx::pbr::LightType::Directional) continue;

				osg::Vec3 direction = _lights.getDirection(index);
				osg::Vec3 lightColorValue = _lights.getColor(index);

				// `direction` is the ray TRAVEL direction (light -> surface; see LIGHT_UNIFORMS'
				// comment in PBR.hpp and osgx_DirectionalLightRadiance's `L = -normalize(direction)`
				// -- L, surface-to-light, is the negation). The plane represents the "wall of
				// parallel rays" and belongs on the light-SOURCE side of the object, i.e. offset
				// against travel direction (-direction, not +direction); the arrow then points
				// FROM that plane TOWARD the object, which is the true travel direction (+direction)
				// -- both were flipped (offset by +direction, arrow pointing -direction) before this
				// fix, which put the plane on the wrong side and made the arrow point backward.
				float len = direction.length();
				osg::Vec3 n = len > 1e-6f ? -direction / len : osg::Vec3(0.0f, 0.0f, -1.0f);
				osg::Vec3 center = _center + n * _normalLen;
				osg::Vec3 u, v;

				detail::perpendicularBasis(n, u, v);

				std::size_t start = static_cast<std::size_t>(i) * 10;

				(*verts2)[start + 0] = center + u * _planeHalf + v * _planeHalf;
				(*verts2)[start + 1] = center - u * _planeHalf + v * _planeHalf;
				(*verts2)[start + 2] = center - u * _planeHalf - v * _planeHalf;
				(*verts2)[start + 3] = center + u * _planeHalf - v * _planeHalf;
				(*verts2)[start + 4] = center;

				osg::Vec3 tip = center - n * _normalLen;

				(*verts2)[start + 5] = tip;

				osg::Vec3 wingBase = tip + n * _arrowBack;

				(*verts2)[start + 6] = tip;
				(*verts2)[start + 7] = wingBase + u * _arrowWidth;
				(*verts2)[start + 8] = tip;
				(*verts2)[start + 9] = wingBase - u * _arrowWidth;

				osg::Vec3 c = detail::gizmoColor(lightColorValue);

				for(std::size_t k = 0; k < 10; k++) (*colors2)[start + k] = c;

				geom2->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, static_cast<GLint>(start), 4));
				geom2->addPrimitiveSet(new osg::DrawArrays(GL_LINES, static_cast<GLint>(start) + 4, 6));
			}

			verts2->dirty();
			colors2->dirty();
			geom2->dirtyBound();

			traverse(node, nv);
		}

		osgx::pbr::LightSet _lights;
		osg::Vec3 _center;
		float _planeHalf, _normalLen, _arrowBack, _arrowWidth;
	};

	geode->setUpdateCallback(
		new DirectionalOverlayCallback(lights, boundCenter, planeHalfSize, normalLength, arrowBack, arrowWidth)
	);

	auto camera = osgx::make_ref<osg::Camera>();

	camera->setReferenceFrame(osg::Transform::RELATIVE_RF);
	camera->setRenderOrder(osg::Camera::POST_RENDER);
	camera->setClearMask(0);
	camera->setAllowEventFocus(false);
	camera->addChild(geode.get());

	return camera;
}

LightGizmos createLightGizmos(
	const osgx::pbr::LightSet& lights,
	osg::Node* scene,
	float minMarkerRadius,
	float spotConeLength
) {
	LightGizmos gizmos;

	gizmos.markers = osgx::make_ref<LightMarkers>(lights, minMarkerRadius, spotConeLength);
	gizmos.overlay = createDirectionalOverlay(lights, scene);

	return gizmos;
}

}
