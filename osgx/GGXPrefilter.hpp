#pragma once

#include "Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Group>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>

OSGX_ENABLE_WARNINGS

namespace osg { class Image; }

namespace osgx::ibl {

// TODO: If these can't be NEGATIVE, they need to be OTHER TYPES than `int`!
struct GGXPrefilterOptions {
	int prefilterSize = 128;
	int maxFrames = 8;
	int readbackFrame = 2;
	// bool configureGLContext = true;

	// If true, GGXPrefilterReadback calls glFinish() immediately before reading the
	// baked cubemap back from the GPU (deterministic, but stalls the
	// pipeline). If false, it trusts that `readbackFrame` frames having
	// already elapsed is enough for the GPU to have caught up, and skips
	// the stall. TODO: no caller flips this yet; it exists so a future
	// best-effort/async bake mode can do so without touching GGXPrefilterReadback.
	bool syncReadback = true;
};

// Sets the OSG_GL_* / OSG_THREADING environment variables so that a graphics
// context created afterward (e.g. by an osgViewer::Viewer) is compatible with
// the GLSL 4.60 prefilter shaders. Must be called before that context exists.
// void configureIBLGLContext();

// Post-draw callback that, once attached to a rendering camera, waits until
// `triggerFrame` frames have been rendered and then reads the prefiltered
// cubemap back from the GPU into `getResult()`.
class GGXPrefilterReadback: public osg::Camera::DrawCallback {
public:
	GGXPrefilterReadback(osg::TextureCubeMap* srcTex, int triggerFrame, bool sync);

	void operator()(osg::RenderInfo& ri) const override;

	bool isDone() const { return done; }
	osg::TextureCubeMap* getResult() const { return result; }
	void reset();

private:
	osg::ref_ptr<osg::TextureCubeMap> srcTex;
	int triggerFrame = 0;
	bool sync = true;
	mutable int frameCount = 0;
	mutable osg::ref_ptr<osg::TextureCubeMap> result;
	mutable bool done = false;
};

struct GGXPrefilterScene {
	osg::ref_ptr<osg::Group> root;
	osg::ref_ptr<osg::Texture2D> sourceTexture;
	osg::ref_ptr<osg::TextureCubeMap> prefilterTexture;
	osg::ref_ptr<GGXPrefilterReadback> readback;
};

// Builds the offscreen scene graph (PRE_RENDER cameras, one per cubemap
// face/mip) that GGX-prefilters `equirectImage`. Does not render anything:
// the caller owns the graphics context/viewer, is responsible for setting
// `root` as scene data, attaching `readback` as a post-draw callback on the
// camera that will actually render frames, and running frames until
// `readback->isDone()`.
GGXPrefilterScene createGGXPrefilterScene(
	osg::Image* equirectImage,
	const GGXPrefilterOptions& options = {}
);

// Reuses an existing bake scene for a new equirectangular source image. This
// avoids rebuilding all PRE_RENDER cameras/FBOs/programs for live rebakes.
bool rebakeGGXPrefilterScene(GGXPrefilterScene& scene, osg::Image* equirectImage);

// Applies the standard cubemap filter/wrap settings to a completed bake.
// Only valid to call once `readback->isDone()`.
osg::ref_ptr<osg::TextureCubeMap> finishGGXPrefilter(GGXPrefilterReadback* readback);

}
