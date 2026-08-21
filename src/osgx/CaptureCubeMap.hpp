#pragma once

#include "IBL.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Group>
#include <osg/Node>
#include <osg/TextureCubeMap>
#include <osg/Vec3d>
#include <osg/Vec4>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <array>

namespace osgx {

// Generic radiance-cubemap capture settings. Filtering, file formats, and lighting semantics are
// deliberately outside this layer: it only renders a scene through six ordinary perspective views.
struct CaptureCubeMapOptions {
	int cubeSize = 256;
	double nearPlane = 0.1;
	double farPlane = 1000.0;
	osg::Vec4 clearColor = osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
};

// A retained, frame-driven cubemap capture. Add root to a rendered scene graph and advance frames;
// ready() becomes true after all six faces have rendered once. The captured node may also belong to
// the application's visible scene. Capture cameras skip child update traversal, so shared update
// callbacks run through the application's normal scene path rather than six times per capture.
struct CaptureCubeMapScene {
	osg::ref_ptr<osg::Group> root;
	osg::ref_ptr<osg::TextureCubeMap> radianceTexture;
	osg::ref_ptr<BakeCompletion> completion;
	std::array<osg::ref_ptr<osg::Camera>, 6> cameras;

	bool ready() const;

	static CaptureCubeMapScene create(
		osg::Node* capturedNode,
		const osg::Vec3d& position,
		const CaptureCubeMapOptions& options={}
	);

	// Reuses the existing six cameras and output texture from a new capture position. The captured
	// node itself remains unchanged; mutate that scene normally before requesting another capture.
	bool recapture(const osg::Vec3d& position);
};

}
