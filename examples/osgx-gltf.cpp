// vimrun! ./examples/osgx-gltf model.gltf --ktx2 papermill.ktx2 --hdr papermill.hdr
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
		std::string(args.getApplicationName()) + " <model.gltf> --ktx2 <path> --hdr <path> [--debug [mode]]"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--ktx2 <path>",
		"Pre-filtered environment cubemap"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--hdr <path>",
		"Source HDR environment (for Lambertian diffuse irradiance)"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--debug [mode]",
		"combined, diffuse, specular, base-color, roughness, metallic, normal-texture, normal-texture-raw, geometry-normal, shading-normal, geometry-tangent, bitangent, linear-diffuse, linear-specular, or linear-combined"
	);

	std::string ktx2Path, hdrPath, debugName = "combined";
	const std::map<std::string, int> debugModes = {
		{"combined", 0}, {"diffuse", 1}, {"specular", 2},
		{"base-color", 3}, {"roughness", 4}, {"metallic", 5},
		{"normal-texture", 6}, {"normal-texture-raw", 7}, {"geometry-normal", 8},
		{"shading-normal", 9}, {"geometry-tangent", 10}, {"bitangent", 11},
		{"linear-diffuse", 12}, {"linear-specular", 13}, {"linear-combined", 14}
	};

	const bool haveKtx2 = args.read("--ktx2", ktx2Path);
	const bool haveHdr = args.read("--hdr", hdrPath);
	const int debugPos = args.find("--debug");
	const bool diagnostics = debugPos >= 0;

	if(diagnostics) {
		const bool hasMode = debugPos + 1 < args.argc() && debugModes.contains(args[debugPos + 1]);

		if(hasMode) args.read(debugPos, "--debug", debugName);
		else args.read(debugPos, "--debug");
	}

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

	auto pis = osgx::gltf::createPBRIBLScene(model.get(), ktx2Path, hdrPath, 1.0f, 1024, diagnostics);

	if(!pis.valid()) {
		std::cerr << "createPBRIBLScene failed to load " << ktx2Path << " / " << hdrPath << std::endl;

		return 1;
	}

	const auto debug = debugModes.find(debugName);

	if(diagnostics && debug == debugModes.end()) {
		std::cerr << "Unknown --debug mode: " << debugName << std::endl;

		return 1;
	}

	if(diagnostics) pis.debugMode->set(debug->second);

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(pis.lutCamera);
	root->addChild(model);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.getCamera()->setClearColor(osg::Vec4f(
		48.0f / 255.0f,
		53.0f / 255.0f,
		66.0f / 255.0f,
		1.0f
	)); // #303542

	// Diagnostics, ported from pyosg-khronos-viewer.py: 1/2/3 pick debugMode; N/R toggle the
	// normal/roughness maps.
	if(diagnostics) std::cout <<
		"Diagnostics: 1=combined 2=diffuse 3=specular N=toggle normal map "
		"R=toggle roughness map" << std::endl
	;

	if(diagnostics) viewer.addEventHandler(new osgx::LambdaKeyHandler(
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
