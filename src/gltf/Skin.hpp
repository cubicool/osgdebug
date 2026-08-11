#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Array>
#include <osg/Callback>
#include <osg/MatrixTransform>
#include <osg/Referenced>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <string>
#include <vector>

namespace tinygltf { class Model; }

namespace osgx::gltf::detail {

struct Skin: public osg::Referenced {
	int index = -1;
	std::string name;
	std::vector<int> joints;
	std::vector<osg::Matrixf> inverseBindMatrices;
	std::vector<osg::observer_ptr<osg::MatrixTransform>> jointNodes;
	std::vector<osg::observer_ptr<osg::MatrixTransform>> skinnedNodes;
	int skeleton = -1;
	osg::ref_ptr<osg::MatrixfArray> paletteMatrices;

	// Index of each joint's parent joint within this skin, or -1 when its parent lies outside it.
	std::vector<int> parentJointIndex;

	// Scratch buffers reused for every palette update to avoid per-frame allocation.
	std::vector<osg::Matrixd> jointWorldCache;
	std::vector<char> jointWorldComputed;

	void initPalette();
	osg::Matrixd computeJointWorld(std::size_t jointIndex);
	bool updatePalette(osg::Node* skinnedNode);
};

class SkinPaletteCallback: public osg::NodeCallback {
public:
	explicit SkinPaletteCallback(Skin* skin);

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

private:
	osg::ref_ptr<Skin> _skin;
	bool _loggedOnce = false;
};

std::vector<osg::ref_ptr<Skin>> prepareSkins(
	const tinygltf::Model& model,
	const std::vector<osg::ref_ptr<osg::Array>>& arrays
);

void resolveSkinJointNodes(
	const tinygltf::Model& model,
	const std::vector<osg::observer_ptr<osg::MatrixTransform>>& nodeTransforms,
	const std::vector<osg::ref_ptr<Skin>>& skins
);

void installSkinPaletteCallbacks(const std::vector<osg::ref_ptr<Skin>>& skins);

}
