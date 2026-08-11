#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osgDB/ReaderWriter>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace osgx::gltf {

class Reader {
public:
	// Fixed, strictly sequential progress stages for read(). Within a stage, current is
	// non-decreasing, and every stage emits a final current == total tick before the next stage.
	enum class Stage { Parsing, LoadingTextures, BuildingNodes };

	static constexpr std::string_view stageName(Stage stage) {
		switch(stage) {
			case Stage::Parsing: return "parsing";
			case Stage::LoadingTextures: return "loading_textures";
			case Stage::BuildingNodes: return "building_nodes";
		}

		return "";
	}

	using ProgressCallback = std::function<void(
		Stage stage,
		std::size_t current,
		std::size_t total
	)>;

	// Shared, thread-safe texture cache. The cache must outlive every Reader that refers to it.
	class TextureCache {
	public:
		TextureCache();
		~TextureCache();

		TextureCache(const TextureCache&) = delete;
		TextureCache& operator=(const TextureCache&) = delete;
		TextureCache(TextureCache&&) noexcept;
		TextureCache& operator=(TextureCache&&) noexcept;

	private:
		struct Implementation;
		std::unique_ptr<Implementation> _implementation;

		friend class Reader;
	};

	void setTextureCache(TextureCache* cache) const { _textureCache = cache; }

	osgDB::ReaderWriter::ReadResult read(
		const std::string& location,
		bool isBinary,
		const osgDB::Options* options,
		const ProgressCallback& progress = nullptr
	) const;

private:
	mutable TextureCache* _textureCache = nullptr;
};

}
