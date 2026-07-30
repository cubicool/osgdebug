#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS

#include "ReaderImpl.hpp"
#include "Log.hpp"
#include "Scene.hpp"

OSGX_DISABLE_WARNINGS

#include <osgDB/FileNameUtils>
#include <osgDB/Options>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <string>

namespace {

using ProgressCallback = osgx::gltf::Reader::ProgressCallback;
using Stage = osgx::gltf::Reader::Stage;

struct ImageLoadContext {
	const ProgressCallback* _progress = nullptr;
	size_t _imagesLoaded = 0;
	size_t _totalImages = 0;
};

std::string expandFilePath(const std::string& filepath, void* userData) {
	const std::string& referrer = *static_cast<const std::string*>(userData);

	std::string path = osgDB::getRealPath(
		osgDB::isAbsolutePath(filepath) ? filepath :
		osgDB::concatPaths(osgDB::getFilePath(referrer), filepath)
	);

	return tinygltf::ExpandFilePath(path, userData);
}

bool skipImageLoad(
	tinygltf::Image*,
	const int,
	std::string*,
	std::string*,
	int,
	int,
	const unsigned char*,
	int,
	void*
) {
	return true;
}

bool loadImage(
	tinygltf::Image* image,
	const int imageIdx,
	std::string* err,
	std::string* warn,
	int reqWidth,
	int reqHeight,
	const unsigned char* bytes,
	int size,
	void* userData
) {
	auto* context = static_cast<ImageLoadContext*>(userData);

	// Pass nullptr as LoadImageData's own user_data. A non-null value is interpreted
	// as tinygltf::LoadImageDataOption, not our progress context.
	bool ok = tinygltf::LoadImageData(
		image,
		imageIdx,
		err,
		warn,
		reqWidth,
		reqHeight,
		bytes,
		size,
		nullptr
	);

	if(context && context->_progress && *context->_progress) {
		context->_imagesLoaded++;
		(*context->_progress)(
			Stage::LoadingTextures,
			context->_imagesLoaded,
			context->_totalImages
		);
	}

	return ok;
}

void logAnimationBits(const tinygltf::Model& model) {
	if(model.skins.empty() && model.animations.empty()) return;

	GLTF_NOTIFY(0)
		<< model.skins.size() << " skin(s), "
		<< model.animations.size() << " animation(s)" << std::endl
	;

	for(size_t skinIdx = 0; skinIdx < model.skins.size(); skinIdx++) {
		const auto& skin = model.skins[skinIdx];

		GLTF_NOTIFY(1)
			<< "skin[" << skinIdx << "] '" << skin.name << "'"
			<< " joints=" << skin.joints.size()
			<< " skeleton=" << skin.skeleton
			<< " inverseBindMatrices=" << skin.inverseBindMatrices << std::endl
		;

		for(size_t jointIdx = 0; jointIdx < skin.joints.size(); jointIdx++) {
			int nodeIdx = skin.joints[jointIdx];
			const char* nodeName =
				nodeIdx >= 0 && nodeIdx < static_cast<int>(model.nodes.size())
				? model.nodes[static_cast<size_t>(nodeIdx)].name.c_str()
				: ""
			;

			GLTF_NOTIFY(2)
				<< "joint[" << jointIdx << "]"
				<< " node=" << nodeIdx
				<< " '" << nodeName << "'" << std::endl
			;
		}
	}

	for(size_t animIdx = 0; animIdx < model.animations.size(); animIdx++) {
		const auto& animation = model.animations[animIdx];

		GLTF_NOTIFY(1)
			<< "animation[" << animIdx << "] '" << animation.name << "'"
			<< " channels=" << animation.channels.size()
			<< " samplers=" << animation.samplers.size() << std::endl
		;

		for(size_t samplerIdx = 0; samplerIdx < animation.samplers.size(); samplerIdx++) {
			const auto& sampler = animation.samplers[samplerIdx];

			GLTF_NOTIFY(2)
				<< "sampler[" << samplerIdx << "]"
				<< " input=" << sampler.input
				<< " output=" << sampler.output
				<< " interpolation=" << sampler.interpolation << std::endl
			;
		}

		for(size_t channelIdx = 0; channelIdx < animation.channels.size(); channelIdx++) {
			const auto& channel = animation.channels[channelIdx];
			const char* nodeName =
				channel.target_node >= 0 &&
				channel.target_node < static_cast<int>(model.nodes.size())
				? model.nodes[static_cast<size_t>(channel.target_node)].name.c_str()
				: ""
			;

			GLTF_NOTIFY(2)
				<< "channel[" << channelIdx << "]"
				<< " sampler=" << channel.sampler
				<< " targetNode=" << channel.target_node
				<< " '" << nodeName << "'"
				<< " path=" << channel.target_path << std::endl
			;
		}
	}
}

}

namespace osgx::gltf::detail {

osgDB::ReaderWriter::ReadResult ReaderImpl::read(
	const std::string& location,
	bool isBinary,
	const osgDB::Options* readOptions,
	const Reader::ProgressCallback& progress
) const {
	std::string err, warn;
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	ImageLoadContext imageLoadContext;

	tinygltf::FsCallbacks fs;

	fs.FileExists = &tinygltf::FileExists;
	fs.ExpandFilePath = &expandFilePath;
	fs.ReadWholeFile = &tinygltf::ReadWholeFile;
	fs.WriteWholeFile = &tinygltf::WriteWholeFile;
	fs.user_data = const_cast<std::string*>(&location);

	loader.SetFsCallbacks(fs);

	GLTF_NOTIFY(0) << "loading " << location << std::endl;

	if(progress) progress(Stage::Parsing, 0, 1);

	// Parse metadata without decoding images so texture progress has a real total.
	{
		tinygltf::Model countModel;
		tinygltf::TinyGLTF counter;
		std::string countErr, countWarn;

		counter.SetFsCallbacks(fs);
		counter.SetImageLoader(&skipImageLoad, nullptr);

		bool countOk = isBinary
			? counter.LoadBinaryFromFile(&countModel, &countErr, &countWarn, location)
			: counter.LoadASCIIFromFile(&countModel, &countErr, &countWarn, location)
		;

		imageLoadContext._totalImages = countOk ? countModel.images.size() : 0;
	}

	if(progress) progress(Stage::Parsing, 1, 1);

	imageLoadContext._progress = &progress;

	if(progress) progress(Stage::LoadingTextures, 0, imageLoadContext._totalImages);

	loader.SetImageLoader(&loadImage, &imageLoadContext);

	bool ok = isBinary
		? loader.LoadBinaryFromFile(&model, &err, &warn, location)
		: loader.LoadASCIIFromFile(&model, &err, &warn, location)
	;

	if(!warn.empty()) OSG_WARN << "" << location << ": " << warn << std::endl;

	if(!ok || !err.empty()) {
		OSG_WARN << "failed to load " << location << ": " << err << std::endl;

		return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
	}

	GLTF_NOTIFY(0)
		<< model.meshes.size() << " mesh(es), "
		<< model.accessors.size() << " accessor(s), "
		<< model.bufferViews.size() << " bufferView(s), "
		<< model.buffers.size() << " buffer(s), "
		<< model.images.size() << " image(s)" << std::endl
	;

	logAnimationBits(model);

	return buildScene(
		model,
		location,
		readOptions,
		_textureCache,
		progress
	);
}

}
