#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS

#include "Texture.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>

#include <osgDB/FileNameUtils>
#include <osgDB/ReadFile>

OSGX_ENABLE_WARNINGS

#include <cstring>

#ifndef GL_SRGB8
# define GL_SRGB8 0x8C41
#endif
#ifndef GL_SRGB8_ALPHA8
# define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace osgx::gltf::detail {

osg::Texture2D* TextureCache::find(const std::string& key) const {
	std::lock_guard<std::mutex> lock(_mutex);
	auto match = _textures.find(key);

	if(match == _textures.end()) return nullptr;

	return match->second;
}

void TextureCache::store(const std::string& key, osg::Texture2D* texture) {
	std::lock_guard<std::mutex> lock(_mutex);

	_textures[key] = texture;
}

TextureLoader::TextureLoader(
	const tinygltf::Model& model,
	const std::string& referrer,
	const osgDB::Options* readOptions,
	TextureCache* cache
):
_model(model),
_referrer(referrer),
_readOptions(readOptions),
_cache(cache) {}

osg::Image* TextureLoader::loadRawImage(int textureIndex) const {
	if(textureIndex < 0 || textureIndex >= static_cast<int>(_model.textures.size())) return nullptr;

	const tinygltf::Texture& texture = _model.textures[static_cast<std::size_t>(textureIndex)];

	if(texture.source < 0 || texture.source >= static_cast<int>(_model.images.size())) return nullptr;

	const tinygltf::Image& source = _model.images[static_cast<std::size_t>(texture.source)];
	osg::ref_ptr<osg::Image> image;

	if(!source.image.empty()) {
		const GLenum pixelFormat = source.component == 4 ? GL_RGBA : GL_RGB;
		const GLint internalFormat = source.component == 4
			? static_cast<GLint>(GL_RGBA8)
			: static_cast<GLint>(GL_RGB8)
		;
		auto* data = new unsigned char[source.image.size()];

		std::memcpy(data, source.image.data(), source.image.size());

		image = new osg::Image();
		image->setImage(
			source.width,
			source.height,
			1,
			internalFormat,
			pixelFormat,
			GL_UNSIGNED_BYTE,
			data,
			osg::Image::USE_NEW_DELETE
		);
	}
	else if(!source.uri.empty() && !tinygltf::IsDataURI(source.uri)) {
		const std::string path = osgDB::concatPaths(
			osgDB::getFilePath(_referrer),
			source.uri
		);

		image = osgDB::readImageFile(path, _readOptions);

		if(image) image->flipVertical();
	}

	return image.release();
}

void TextureLoader::applyFormatAndSampler(
	osg::Texture2D* texture,
	osg::Image* image,
	bool sRGB,
	int samplerIndex
) const {
	if(image->getPixelFormat() == GL_RGB) {
		image->setInternalTextureFormat(sRGB ? GL_SRGB8 : GL_RGB8);
	}
	if(image->getPixelFormat() == GL_RGBA) {
		image->setInternalTextureFormat(sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8);
	}

	texture->setResizeNonPowerOfTwoHint(false);
	texture->setDataVariance(osg::Object::STATIC);
	texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
	texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	texture->setMaxAnisotropy(16.0f);

	if(samplerIndex >= 0 && samplerIndex < static_cast<int>(_model.samplers.size())) {
		const tinygltf::Sampler& sampler =
			_model.samplers[static_cast<std::size_t>(samplerIndex)];

		texture->setWrap(
			osg::Texture::WRAP_S,
			static_cast<osg::Texture::WrapMode>(sampler.wrapS)
		);
		texture->setWrap(
			osg::Texture::WRAP_T,
			static_cast<osg::Texture::WrapMode>(sampler.wrapT)
		);
	}
	else {
		texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
		texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	}
}

osg::Texture2D* TextureLoader::getOrCreateTexture(int textureIndex, bool sRGB) const {
	if(textureIndex < 0 || textureIndex >= static_cast<int>(_model.textures.size())) return nullptr;

	const tinygltf::Texture& texture = _model.textures[static_cast<std::size_t>(textureIndex)];

	if(texture.source < 0 || texture.source >= static_cast<int>(_model.images.size())) return nullptr;

	const tinygltf::Image& image = _model.images[static_cast<std::size_t>(texture.source)];
	const bool dataURI = !image.uri.empty() && tinygltf::IsDataURI(image.uri);
	const bool externalImage = !image.uri.empty() && !dataURI;
	// tinygltf decodes every image, including an ordinary external PNG, into
	// image.image. That describes temporary decoded data, not the glTF asset's
	// identity: using it to identify embedded images made every external texture
	// miss the cache and copy itself once per primitive during scene construction.
	const bool unrefImageDataAfterApply = !image.image.empty() || dataURI;
	std::string cacheKey;

	if(externalImage) {
		cacheKey = osgDB::getRealPath(osgDB::concatPaths(
			osgDB::getFilePath(_referrer),
			image.uri
		)) + (sRGB ? "|sRGB" : "|linear");
	}
	else cacheKey = _referrer + "|image:" + std::to_string(texture.source) +
		(sRGB ? "|sRGB" : "|linear");

	if(osg::Texture2D* cached = findCached(cacheKey)) return cached;

	osg::ref_ptr<osg::Image> loadedImage = loadRawImage(textureIndex);

	if(!loadedImage) return nullptr;

	osg::ref_ptr<osg::Texture2D> osgTexture = new osg::Texture2D(loadedImage);

	applyFormatAndSampler(osgTexture, loadedImage, sRGB, texture.sampler);
	osgTexture->setUnRefImageDataAfterApply(unrefImageDataAfterApply);
	cache(cacheKey, osgTexture);

	return osgTexture.release();
}

osg::Texture2D* TextureLoader::findCached(const std::string& key) const {
	return _cache && !key.empty() ? _cache->find(key) : nullptr;
}

void TextureLoader::cache(const std::string& key, osg::Texture2D* texture) const {
	if(_cache && !key.empty()) _cache->store(key, texture);
}

}
