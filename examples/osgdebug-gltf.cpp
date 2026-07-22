// vimrun! ./examples/osgdebug-gltf model.gltf --ktx2 papermill.ktx2 --hdr papermill.hdr
//
// Minimal proof that osgx/GLTF.hpp works from plain C++, not just via the Python
// bindings (already confirmed against OpenSceneGraph.py/pyosg-lighting). One call
// to osgx::gltf::setupFullPBR() wires up material glue (osgx::gltf) + full PBR/IBL
// (osgx::pbr + osgx::ibl) against a real glTF model -- one direct light, no manual
// shader assembly. See osgx::gltf::setupFullPBR()'s comments in osgx/GLTF.hpp for
// what it does under the hood, and the file banner there for where this pattern
// came from.
//
// lutCamera (returned by setupFullPBR) MUST be added to the scene graph or the
// BRDF LUT never bakes; it's ABSOLUTE_RF, so it doesn't matter where.

#include "../osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osgDB/ReadFile>
#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <iostream>

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	args.getApplicationUsage()->setCommandLineUsage(
		std::string(args.getApplicationName()) + " <model.gltf> --ktx2 <path> --hdr <path>"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--ktx2 <path>",
		"Pre-filtered environment cubemap"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--hdr <path>",
		"Source HDR environment (for SH9 diffuse irradiance)"
	);

	std::string ktx2Path, hdrPath;

	const bool haveKtx2 = args.read("--ktx2", ktx2Path);
	const bool haveHdr = args.read("--hdr", hdrPath);

	if(args.argc() < 2 || !haveKtx2 || !haveHdr) {
		args.getApplicationUsage()->write(std::cerr);

		return 1;
	}

	osg::ref_ptr<osg::Node> model = osgDB::readRefNodeFile(args[1]);

	if(!model) {
		std::cerr << "Failed to load: " << args[1] << std::endl;

		return 1;
	}

	auto setup = osgx::gltf::setupFullPBR(model.get(), ktx2Path, hdrPath);

	if(!setup.valid()) {
		std::cerr << "setupFullPBR failed to load " << ktx2Path << " / " << hdrPath << std::endl;

		return 1;
	}

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(setup.lutCamera);
	root->addChild(model);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
