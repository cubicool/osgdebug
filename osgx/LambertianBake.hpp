#pragma once

#include "IBL.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Group>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

namespace osgx::ibl {

// Requested quality for a GPU cosine-convolution of an equirectangular HDR. The resulting cube
// stores diffuse irradiance divided by pi, matching the existing CPU Lambertian cube convention.
struct LambertianBakeOptions {
	int cubeSize = 256;
	int sampleCount = 2048;
};

// A frame-driven GPU bake. Add root to a viewer scene, advance frames, then use diffuseTexture
// once completion reports done. `rebakeLambertianBakeScene()` re-arms the same passes for a new
// HDR image without recreating their cameras or output texture.
struct LambertianBakeScene {
	osg::ref_ptr<osg::Group> root;
	osg::ref_ptr<osg::Texture2D> sourceTexture;
	osg::ref_ptr<osg::TextureCubeMap> diffuseTexture;
	osg::ref_ptr<BakeCompletion> completion;

	bool ready() const;
};

LambertianBakeScene createLambertianBakeScene(
	osg::Image* equirectangularHDR,
	const LambertianBakeOptions& options={}
);

bool rebakeLambertianBakeScene(
	LambertianBakeScene& scene,
	osg::Image* equirectangularHDR
);

}
