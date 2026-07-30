// osgx-gltf-iblbake-gpu -- bakes prepared IBL resources to KTX2.
//
// Usage:
//   osgx-gltf-iblbake-gpu <input.hdr> <output.ktx2>
//       [--prefilter-size N] [--samples N] [--diffuse-cube-size N] [--diffuse-samples N]
//     Bakes the GGX-prefiltered specular cubemap (written to <output.ktx2>) and the Lambertian
//     diffuse irradiance cubemap (written alongside it, with a "-lambertian" suffix inserted before
//     the extension) from one HDR equirectangular source image.
//
//   osgx-gltf-iblbake-gpu --brdf-lut-only <output.ktx2> [--lut-size N]
//     Bakes ONLY the split-sum BRDF LUT. Deliberately takes no HDR input at all -- the LUT isn't
//     derived from one; it's a property of this codebase's BRDF implementation (see
//     osgx::ibl::sharedBRDFLUT()'s comments in osgdebug/osgx/IBL.hpp), not of any environment. A
//     separate mode with no HDR argument keeps that true at the CLI surface, not just in the C++
//     API -- pairing "--hdr" with a LUT output would silently re-teach the same false association
//     this mode exists to avoid.

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Notify>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgViewer/Viewer>

#include <osgx/GGXPrefilter.hpp>
#include <osgx/LambertianBake.hpp>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

void configureIBLGLContext() {
#if defined(_WIN32)
	_putenv("OSG_GL_CONTEXT_PROFILE_MASK=1");
	_putenv("OSG_GL_VERSION=4.6");
	_putenv("OSG_GL_CONTEXT_VERSION=4.6");
	_putenv("OSG_THREADING=SingleThreaded");
#else
	setenv("OSG_GL_CONTEXT_PROFILE_MASK", "1", 1);
	setenv("OSG_GL_VERSION", "4.6", 1);
	setenv("OSG_GL_CONTEXT_VERSION", "4.6", 1);
	setenv("OSG_THREADING", "SingleThreaded", 1);
#endif
}

namespace {

// Inserts `suffix` before the extension of `path` -- e.g. withSuffix("foo.ktx2", "-lambertian")
// -> "foo-lambertian.ktx2". Derives the diffuse cubemap's output path from the specular one the
// caller already gave us, so existing scripts pointing at <output.ktx2> for specular keep working
// unchanged.
std::string withSuffix(const std::string& path, std::string_view suffix) {
	std::filesystem::path p(path);

	p.replace_filename(p.stem().string() + std::string(suffix) + p.extension().string());

	return p.string();
}

int runBRDFLUTOnly(const std::string& outputPath, int lutSize) {
	osg::setNotifyLevel(osg::NOTICE);

	auto lut = osgx::ibl::sharedBRDFLUT(lutSize);

	if(!lut.camera) {
		// Can't happen in a fresh process (the cache starts empty every run), but guard rather
		// than silently writing nothing if that assumption ever stops holding.
		OSG_WARN << "osgx-gltf-iblbake-gpu: BRDF LUT unexpectedly already baked in this process" << std::endl;

		return 1;
	}

	osg::ref_ptr<osgx::ibl::BRDFLUTReadback> readback = new osgx::ibl::BRDFLUTReadback(lut.texture);

	auto root = new osg::Group();

	root->addChild(lut.camera);

	osgViewer::Viewer viewer;

	viewer.setUpViewInWindow(0, 0, 128, 128);
	viewer.setSceneData(root);
	viewer.getCamera()->addPostDrawCallback(readback);

	for(int frame = 0; frame < 4 && !readback->isDone(); frame++) viewer.frame();

	if(!readback->isDone()) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: BRDF LUT bake did not complete" << std::endl;

		return 1;
	}

	if(!osgDB::writeObjectFile(*readback->getResult(), outputPath)) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to write " << outputPath << std::endl;

		return 1;
	}

	OSG_NOTICE << "osgx-gltf-iblbake-gpu: wrote " << outputPath << std::endl;

	return 0;
}

}

int main(int argc, char* argv[]) {
	if(argc > 1 && std::string(argv[1]) == "--brdf-lut-only") {
		if(argc < 3) {
			OSG_WARN << "Usage: osgx-gltf-iblbake-gpu --brdf-lut-only <output.ktx2> [--lut-size N]" << std::endl;

			return 1;
		}

		std::string outputPath = argv[2];
		int lutSize = 1024;

		for(int i = 3; i < argc; i++) {
			std::string arg = argv[i];

			if(arg == "--lut-size" && i + 1 < argc) lutSize = std::atoi(argv[++i]);
		}

		return runBRDFLUTOnly(outputPath, lutSize);
	}

	if(argc < 3) {
		OSG_WARN
			<< "Usage: osgx-gltf-iblbake-gpu <input.hdr> <output.ktx2> "
			<< "[--prefilter-size N] [--samples N] [--diffuse-cube-size N] [--diffuse-samples N]"
			<< std::endl
			<< "       osgx-gltf-iblbake-gpu --brdf-lut-only <output.ktx2> [--lut-size N]"
			<< std::endl
		;

		return 1;
	}

	std::string inputPath = argv[1];
	std::string specularPath = argv[2];
	std::string diffusePath = withSuffix(specularPath, "-lambertian");

	osgx::ibl::GGXPrefilterOptions specularOptions;
	osgx::ibl::LambertianBakeOptions diffuseOptions;

	for(int i = 3; i < argc; i++) {
		std::string arg = argv[i];

		if(arg == "--prefilter-size" && i + 1 < argc) specularOptions.prefilterSize = std::atoi(argv[++i]);
		else if(arg == "--samples" && i + 1 < argc) specularOptions.sampleCount = std::atoi(argv[++i]);
		else if(arg == "--diffuse-cube-size" && i + 1 < argc) diffuseOptions.cubeSize = std::atoi(argv[++i]);
		else if(arg == "--diffuse-samples" && i + 1 < argc) diffuseOptions.sampleCount = std::atoi(argv[++i]);
	}

	osg::setNotifyLevel(osg::NOTICE);

	osg::ref_ptr<osg::Image> image = osgDB::readImageFile(inputPath);

	if(!image) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to load HDR image " << inputPath << std::endl;

		return 1;
	}

	osgx::ibl::GGXPrefilterScene specularScene = osgx::ibl::createGGXPrefilterScene(
		image,
		specularOptions
	);

	if(!specularScene.root) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to build specular bake scene" << std::endl;

		return 1;
	}

	osgx::ibl::LambertianBakeScene diffuseScene = osgx::ibl::createLambertianBakeScene(
		image,
		diffuseOptions
	);

	if(!diffuseScene.root) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to build diffuse bake scene" << std::endl;

		return 1;
	}

	osg::ref_ptr<osgx::ibl::LambertianCubeReadback> diffuseReadback = new osgx::ibl::LambertianCubeReadback(
		diffuseScene.diffuseTexture,
		diffuseScene.completion
	);

	auto root = new osg::Group();

	root->addChild(specularScene.root);
	root->addChild(diffuseScene.root);

	osgViewer::Viewer viewer;

	viewer.setUpViewInWindow(0, 0, 128, 128);
	viewer.setSceneData(root);
	// Both readbacks are attached to the OUTER viewer camera, not any bake pass's own camera --
	// same reasoning as GGXPrefilterReadback always has: this must run after every PRE_RENDER bake
	// pass has completed for the frame, which the outer camera's post-draw is guaranteed to see.
	// addPostDrawCallback() nests rather than replaces, so both run every frame.
	viewer.getCamera()->addPostDrawCallback(specularScene.readback);
	viewer.getCamera()->addPostDrawCallback(diffuseReadback);

	const int maxFrames = std::max(1, specularOptions.maxFrames);

	for(
		int frame = 0;
		frame < maxFrames && !(specularScene.readback->isDone() && diffuseReadback->isDone());
		frame++
	) viewer.frame();

	if(!specularScene.readback->isDone()) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: specular bake readback did not complete" << std::endl;

		return 1;
	}

	if(!diffuseReadback->isDone()) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: diffuse bake readback did not complete" << std::endl;

		return 1;
	}

	osg::ref_ptr<osg::TextureCubeMap> specularResult = osgx::ibl::finishGGXPrefilter(
		specularScene.readback
	);

	if(!specularResult || !osgDB::writeObjectFile(*specularResult, specularPath)) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to write " << specularPath << std::endl;

		return 1;
	}

	OSG_NOTICE << "osgx-gltf-iblbake-gpu: wrote " << specularPath << std::endl;

	osg::ref_ptr<osg::TextureCubeMap> diffuseResult = osgx::ibl::finishLambertianCubeReadback(
		diffuseReadback
	);

	if(!diffuseResult || !osgDB::writeObjectFile(*diffuseResult, diffusePath)) {
		OSG_WARN << "osgx-gltf-iblbake-gpu: failed to write " << diffusePath << std::endl;

		return 1;
	}

	OSG_NOTICE << "osgx-gltf-iblbake-gpu: wrote " << diffusePath << std::endl;

	return 0;
}
