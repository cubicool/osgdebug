#pragma once

#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Node>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

namespace osgx {

// A small screen-space silhouette expansion pipeline. `selectionCamera` renders `selected` into
// originalMask, then exactly two fullscreen passes perform a separable max filter: dilateXCamera
// reads originalMask and dilateYCamera reads dilatedX. `expanded` stores mask in .r, a nearest
// selected-source UV in .gb, and its Chebyshev distance in pixels in .a. A caller's final effect
// remains separate: sample both originalMask/expanded and calculate
// `expanded.r * (1.0 - originalMask.r)` there.
//
// The source UV identifies a nearest selected mask pixel in the Chebyshev metric. Equal-distance
// ties retain scan order, so the UV can change at ties; the propagated distance remains stable.
// The UV is useful for sampling a G-buffer's normal/position at the nearby selected surface.
struct Aura {
	osg::ref_ptr<osg::Camera> selectionCamera;
	osg::ref_ptr<osg::Camera> dilateXCamera;
	osg::ref_ptr<osg::Camera> dilateYCamera;
	osg::ref_ptr<osg::Texture2D> originalMask;
	osg::ref_ptr<osg::Texture2D> dilatedX;
	osg::ref_ptr<osg::Texture2D> expanded;
	// Radius in output pixels. Both dilation shaders clamp it to their fixed maximum of 64.
	osg::ref_ptr<osg::Uniform> radius;

	bool valid() const;

	// `selected` can be multi-parented normally: once under the application's visible scene, and
	// once under selectionCamera. The selection camera's protected flat-mask Program wins over a
	// material Program installed below it, so this works for arbitrary already-rendered geometry.
	// Add the returned cameras to the render graph in the listed order. Their PRE_RENDER order
	// numbers are 1, 2, and 3, leaving order 0 for a preceding G-buffer geometry pass.
	static Aura create(osg::Node* selected, int width, int height, int radius=8);
};

}
