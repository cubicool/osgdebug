#pragma once

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osgDB/ReaderWriter>

OSGX_ENABLE_WARNINGS

#include <osgx/gltf/Reader.hpp>

#include <string>

namespace osgDB { class Options; }

namespace osgx::gltf::detail {

class TextureCache;

class ReaderImpl {
public:
	void setTextureCache(TextureCache* textureCache) { _textureCache = textureCache; }

	osgDB::ReaderWriter::ReadResult read(
		const std::string& location,
		bool isBinary,
		const osgDB::Options* readOptions,
		const Reader::ProgressCallback& progress
	) const;

private:
	TextureCache* _textureCache = nullptr;
};

}
