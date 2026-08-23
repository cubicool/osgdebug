#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Node>

OSGX_ENABLE_WARNINGS

#include "osgx/gltf/Reader.hpp"

#include <string>

namespace osgDB { class Options; }
struct tg3_model;

namespace osgx::gltf::detail {

class TextureCache;

osg::Node* buildScene(
	const tg3_model& model,
	const std::string& referrer,
	const osgDB::Options* readOptions,
	TextureCache* textureCache,
	const Reader::ProgressCallback& progress
);

}
