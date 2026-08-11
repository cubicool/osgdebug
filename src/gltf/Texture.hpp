#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Texture2D>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <mutex>
#include <string>
#include <unordered_map>

namespace osg { class Image; }
namespace osgDB { class Options; }
namespace tinygltf { class Model; }

namespace osgx::gltf::detail {

class TextureCache {
public:
	osg::Texture2D* find(const std::string& key) const;
	void store(const std::string& key, osg::Texture2D* texture);

private:
	mutable std::mutex _mutex;
	std::unordered_map<std::string, osg::ref_ptr<osg::Texture2D>> _textures;
};

class TextureLoader {
public:
	TextureLoader(
		const tinygltf::Model& model,
		const std::string& referrer,
		const osgDB::Options* readOptions,
		TextureCache* cache
	);

	osg::Image* loadRawImage(int textureIndex) const;
	void applyFormatAndSampler(
		osg::Texture2D* texture,
		osg::Image* image,
		bool sRGB,
		int samplerIndex
	) const;
	osg::Texture2D* getOrCreateTexture(int textureIndex, bool sRGB) const;

	osg::Texture2D* findCached(const std::string& key) const;
	void cache(const std::string& key, osg::Texture2D* texture) const;

private:
	const tinygltf::Model& _model;
	const std::string& _referrer;
	const osgDB::Options* _readOptions;
	TextureCache* _cache;
};

}
