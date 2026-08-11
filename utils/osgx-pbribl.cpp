// osgx-pbribl -- bake a self-contained osgx_pbribl environment bundle.
//
// Usage:
//   osgx-pbribl <input.hdr> <output-basename>
//       [--prefilter-size N] [--samples N] [--diffuse-cube-size N]
//       [--diffuse-samples N] [--lut-size N]
//
// Produces <basename>-specular.ktx2, <basename>-diffuse.ktx2, and
// <basename>.gltf. The manifest identifies the matching built-in BRDF LUT,
// which the renderer caches and bakes once per process rather than serializing
// an HDR-independent artifact beside every environment.

#include "osgx/GGXPrefilter.hpp"
#include "osgx/LambertianBake.hpp"
#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Group>
#include <osg/Image>
#include <osg/Notify>
#include <osg/TextureCubeMap>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

struct OutputPaths {
	std::filesystem::path specular;
	std::filesystem::path diffuse;
	std::filesystem::path manifest;
};

OutputPaths makeOutputPaths(const std::filesystem::path& basename) {
	const std::string base = basename.string();

	return {
		base + "-specular.ktx2",
		base + "-diffuse.ktx2",
		base + ".gltf"
	};
}

bool writeManifest(
	const std::filesystem::path& path,
	const OutputPaths& outputs,
	int prefilterSize,
	int lutSize
) {
	std::ofstream file(path);

	if(!file) {
		OSG_WARN << "osgx-pbribl: failed to open " << path << std::endl;

		return false;
	}

	// Resource URIs are relative to this manifest, even when the requested basename includes a
	// directory.  The loader resolves them against manifest.parent_path().
	file << "{" << std::endl
		<< "  \"asset\": {\"version\": \"2.0\", \"generator\": \"osgx-pbribl\"}," << std::endl
		<< "  \"extensionsUsed\": [\"osgx_pbribl\"]," << std::endl
		<< "  \"extensions\": {" << std::endl
		<< "    \"osgx_pbribl\": {" << std::endl
		<< "      \"environments\": [{" << std::endl
		<< "        \"specular\": {\"uri\": \"" << outputs.specular.filename().string()
		<< "\", \"prefilterSize\": " << prefilterSize << ", \"lowestMipLevel\": 0}," << std::endl
		<< "        \"diffuse\": {\"uri\": \"" << outputs.diffuse.filename().string() << "\"}," << std::endl
		<< "        \"brdfLUT\": {\"builtin\": \"osgx:split-sum-ggx-v1\", \"size\": " << lutSize << "}" << std::endl
		<< "      }]" << std::endl
		<< "    }" << std::endl
		<< "  }" << std::endl
		<< "}" << std::endl;

	if(!file) {
		OSG_WARN << "osgx-pbribl: failed to write " << path << std::endl;

		return false;
	}

	OSG_NOTICE << "osgx-pbribl: wrote " << path << std::endl;

	return true;
}

void usage(const char* program) {
	std::cerr
		<< "Usage: " << program << " <input.hdr> <output-basename>"
		<< " [--prefilter-size N] [--samples N] [--diffuse-cube-size N]"
		<< " [--diffuse-samples N] [--lut-size N]" << std::endl;
}

}

int main(int argc, char* argv[]) {
	if(argc < 3) {
		usage(argv[0]);

		return 1;
	}

	const std::string inputPath = argv[1];
	const OutputPaths outputs = makeOutputPaths(argv[2]);
	osgx::ibl::GGXPrefilterOptions specularOptions;
	osgx::ibl::LambertianBakeOptions diffuseOptions;
	int lutSize = 1024;

	for(int i = 3; i < argc; ++i) {
		const std::string argument = argv[i];

		if(argument == "--prefilter-size" && i + 1 < argc) specularOptions.prefilterSize = std::atoi(argv[++i]);
		else if(argument == "--samples" && i + 1 < argc) specularOptions.sampleCount = std::atoi(argv[++i]);
		else if(argument == "--diffuse-cube-size" && i + 1 < argc) diffuseOptions.cubeSize = std::atoi(argv[++i]);
		else if(argument == "--diffuse-samples" && i + 1 < argc) diffuseOptions.sampleCount = std::atoi(argv[++i]);
		else if(argument == "--lut-size" && i + 1 < argc) lutSize = std::atoi(argv[++i]);
		else {
			OSG_WARN << "osgx-pbribl: unknown or incomplete option " << argument << std::endl;
			usage(argv[0]);

			return 1;
		}
	}

	if(specularOptions.prefilterSize < 1 || specularOptions.sampleCount < 1 ||
		diffuseOptions.cubeSize < 1 || diffuseOptions.sampleCount < 1 || lutSize < 1) {
		OSG_WARN << "osgx-pbribl: all bake sizes and sample counts must be positive" << std::endl;

		return 1;
	}

	osg::setNotifyLevel(osg::NOTICE);
	auto image = osgDB::readRefImageFile(inputPath);

	if(!image) {
		OSG_WARN << "osgx-pbribl: failed to load HDR image " << inputPath << std::endl;

		return 1;
	}

	auto specular = osgx::ibl::createGGXPrefilterScene(image, specularOptions);
	auto diffuse = osgx::ibl::createLambertianBakeScene(image, diffuseOptions);

	if(!specular.root || !diffuse.root) {
		OSG_WARN << "osgx-pbribl: failed to create cubemap bake passes" << std::endl;

		return 1;
	}

	auto diffuseReadback = new osgx::ibl::LambertianCubeReadback(
		diffuse.diffuseTexture,
		diffuse.completion
	);
	auto root = new osg::Group();

	root->addChild(specular.root);
	root->addChild(diffuse.root);

	osgViewer::Viewer viewer;

	viewer.setUpViewInWindow(0, 0, 128, 128);
	viewer.setSceneData(root);
	// Both cube readbacks live on the outer camera so every PRE_RENDER face pass has completed.
	viewer.getCamera()->addPostDrawCallback(specular.readback);
	viewer.getCamera()->addPostDrawCallback(diffuseReadback);

	const int maxFrames = std::max(1, specularOptions.maxFrames);

	for(int frame = 0; frame < maxFrames; ++frame) {
		if(specular.readback->isDone() && diffuseReadback->isDone()) break;

		viewer.frame();
	}

	if(!specular.readback->isDone() || !diffuseReadback->isDone()) {
		OSG_WARN << "osgx-pbribl: one or more cubemap readbacks did not complete" << std::endl;

		return 1;
	}

	auto specularResult = osgx::ibl::finishGGXPrefilter(specular.readback);
	auto diffuseResult = osgx::ibl::finishLambertianCubeReadback(diffuseReadback);

	if(!specularResult || !osgDB::writeObjectFile(*specularResult, outputs.specular.string())) {
		OSG_WARN << "osgx-pbribl: failed to write " << outputs.specular << std::endl;

		return 1;
	}

	if(!diffuseResult || !osgDB::writeObjectFile(*diffuseResult, outputs.diffuse.string())) {
		OSG_WARN << "osgx-pbribl: failed to write " << outputs.diffuse << std::endl;

		return 1;
	}

	OSG_NOTICE << "osgx-pbribl: wrote " << outputs.specular << std::endl;
	OSG_NOTICE << "osgx-pbribl: wrote " << outputs.diffuse << std::endl;

	return writeManifest(outputs.manifest, outputs, specularOptions.prefilterSize, lutSize) ? 0 : 1;
}
