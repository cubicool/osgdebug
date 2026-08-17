#pragma once

#include "Core.hpp"
#include "PBR.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/NodeCallback>
#include <osg/observer_ptr>
#include <osg/StateSet>

OSGX_ENABLE_WARNINGS

namespace osgx::gizmo {

// Debug visualization for osgx::pbr::LightSet lights -- deliberately not part of osgx::debug,
// which is specifically GL_KHR_debug integration, not visual scene gizmos. Two mechanisms, since
// a directional light and a point/spot/sphere light are genuinely different visualization
// problems (see LightGizmos below): only the latter has a real position to place depth-tested
// geometry at.

// Depth-tested, real scene-space markers for point/sphere/spot lights (up to
// osgx::pbr::MAX_LIGHTS), added as an ordinary child of the lit scene. One osg::Geometry, rebuilt
// in place every update traversal from the live LightSet uniforms (an osg::NodeCallback installed
// on this Group) -- combines Shapes.hpp's Polyhedron::rebuild() "mutate the existing arrays,
// don't replace them" pattern with the per-frame-uniform-read NodeCallback idiom already used by
// PBR.hpp's OrbitLightRig and Picking.hpp's PickCameraSync. Marker shape per active light: three
// orthogonal wireframe circles sized to max(lightSourceRadius, minMarkerRadius) for a point/sphere
// light (so an ideal point light still shows a small marker, a sphere light shows its true
// physical size); a wireframe cone (ring + spokes from the apex) for a spot light, sized by its
// outer cone angle and spotConeLength. A directional light has no position and is never drawn
// here -- see createDirectionalOverlay() below.
class LightMarkers: public osg::Group {
public:
	OSGX_META_Object(osgx_gizmo, LightMarkers)

	LightMarkers() = default;
	// `lights` is the live osgx::pbr::LightSet this marker set visualizes.
	explicit LightMarkers(
		const osgx::pbr::LightSet& lights, float minMarkerRadius=0.05f, float spotConeLength=1.0f
	);
	LightMarkers(const LightMarkers& rhs, const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY):
	osg::Group(rhs, co) {}

private:
	class UpdateCallback: public osg::NodeCallback {
	public:
		UpdateCallback(const osgx::pbr::LightSet& lights, float minMarkerRadius, float spotConeLength):
		_lights(lights),
		_minMarkerRadius(minMarkerRadius),
		_spotConeLength(spotConeLength) {}

		void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

	private:
		osgx::pbr::LightSet _lights;
		float _minMarkerRadius;
		float _spotConeLength;
	};

	void rebuild(const osgx::pbr::LightSet& lights, float minMarkerRadius, float spotConeLength);

	osg::ref_ptr<osg::Geometry> _geometry;
};

// Non-depth-tested POST_RENDER overlay for directional lights -- a directional light has no
// position, so there is no real depth to test its marker against. Ports
// create_light_gizmo()/LightGizmoCallback/GIZMO_VERTEX_SHADER/GIZMO_FRAGMENT_SHADER from
// OpenSceneGraph.py/examples/pyosg-lighting/11-sketchfab-lambertian.py (wireframe plane
// perpendicular to the light direction plus a direction arrow) essentially unchanged, generalized
// from one hardcoded light_dir_u/light_color_u pair to LightSet's up-to-MAX_LIGHTS directional
// slots. `scene`'s bounding sphere (computed once, at creation time, same as the Python original)
// sizes and places every directional light's plane/arrow proportionally to the scene.
osg::ref_ptr<osg::Camera> createDirectionalOverlay(const osgx::pbr::LightSet& lights, osg::Node* scene);

// Bundles both mechanisms -- one call for any osgx::pbr-lit scene using osgx::pbr::LightSet,
// instead of a caller hand-wiring LightMarkers and createDirectionalOverlay separately into every
// example.
struct LightGizmos {
	osg::ref_ptr<osg::Group> markers;   // add as a child of the lit scene
	osg::ref_ptr<osg::Camera> overlay;  // add to the viewer as an extra POST_RENDER pass
};

// `minMarkerRadius`/`spotConeLength` forward straight to LightMarkers -- their defaults are
// unit-scene-scale, and a caller whose lights sit much farther from the target than that (a spot
// light standing well back from its subject, say) needs to size them up or the cone/sphere markers
// draw too small/short to visually reach anything, exactly as small and disconnected from the lit
// object as the defaults would otherwise be for a larger scene.
LightGizmos createLightGizmos(
	const osgx::pbr::LightSet& lights,
	osg::Node* scene,
	float minMarkerRadius=0.05f,
	float spotConeLength=1.0f
);

}
