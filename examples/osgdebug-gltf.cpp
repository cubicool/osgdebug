// vimrun! ./examples/osgdebug-gltf model.gltf --ktx2 papermill.ktx2 --hdr papermill.hdr
//
// Minimal proof that osgx/GLTF.hpp works from plain C++, not just via the Python
// bindings (already confirmed against OpenSceneGraph.py/pyosg-lighting). One call
// to osgx::gltf::createPBRIBLScene() wires up material glue (osgx::gltf) + full PBR/IBL
// (osgx::pbr + osgx::ibl) against a real glTF model -- IBL only, no manual shader
// assembly. See osgx::gltf::createPBRIBLScene()'s comments in osgx/GLTF.hpp for
// what it does under the hood, and the file banner there for where this pattern
// came from.
//
// lutCamera (returned by createPBRIBLScene) MUST be added to the scene graph or the
// BRDF LUT never bakes; it's ABSOLUTE_RF, so it doesn't matter where.

#include "../osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osgDB/ReadFile>
#include <osgDB/Registry>
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

	// ReaderWriterGLTF registers this same alias in its own constructor, but that
	// constructor only runs *after* the registry has already resolved which plugin
	// library to dlopen for a given extension -- too late for a cold ".glb" load.
	// Registering it here first breaks the chicken-and-egg.
	osgDB::Registry::instance()->addFileExtensionAlias("glb", "gltf");

	osg::ref_ptr<osg::Node> model = osgDB::readRefNodeFile(args[1]);

	if(!model) {
		std::cerr << "Failed to load: " << args[1] << std::endl;

		return 1;
	}

	auto pis = osgx::gltf::createPBRIBLScene(model.get(), ktx2Path, hdrPath);

	if(!pis.valid()) {
		std::cerr << "createPBRIBLScene failed to load " << ktx2Path << " / " << hdrPath << std::endl;

		return 1;
	}

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(pis.lutCamera);
	root->addChild(model);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());

	// Diagnostics, ported from pyosg-khronos-viewer.py: 1/2/3 pick debugMode, N/R toggle the
	// normal/roughness maps.
	std::cout <<
		"Diagnostics: 1=combined 2=diffuse 3=specular N=toggle normal map "
		"R=toggle roughness map" << std::endl
	;

	viewer.addEventHandler(new osgx::LambdaKeyHandler(
		{'1', '2', '3', 'n', 'N', 'r', 'R'},
		[pis](const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter&, int key) {
			switch(key) {
				case '1': {
					pis.debugMode->set(0);

					std::cout << "[diagnostic] combined" << std::endl;

					break;
				}

				case '2': {
					pis.debugMode->set(1);

					std::cout << "[diagnostic] diffuse only" << std::endl;

					break;
				}

				case '3': {
					pis.debugMode->set(2);

					std::cout << "[diagnostic] specular only" << std::endl;

					break;
				}

				case 'n': case 'N': {
					int v = 0;

					pis.disableNormalMap->get(v);
					pis.disableNormalMap->set(1 - v);

					std::cout << "[diagnostic] normal map " << (v ? "on" : "off") << std::endl;

					break;
				}

				case 'r': case 'R': {
					int v = 0;

					pis.disableRoughnessMap->get(v);
					pis.disableRoughnessMap->set(1 - v);

					std::cout << "[diagnostic] roughness map " << (v ? "on" : "off") << std::endl;

					break;
				}

				default: return false;
			}

			return true;
		}
	));

	return viewer.run();
}
