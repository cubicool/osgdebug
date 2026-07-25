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
#include <osgDB/WriteFile>
#include <osg/DisplaySettings>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <atomic>
#include <iostream>

namespace {

class FramebufferPNG: public osg::Camera::DrawCallback {
public:
	FramebufferPNG(osg::Camera* camera, std::string filename):
		_camera(camera),
		_filename(std::move(filename)) {}

	void operator()(osg::RenderInfo&) const override {
		if(_started.exchange(true, std::memory_order_acq_rel)) return;

		const auto* viewport = _camera->getViewport();

		if(!viewport || !viewport->valid()) {
			OSG_WARN << "Capture camera has no viewport" << std::endl;
			_done.store(true, std::memory_order_release);

			return;
		}

		auto image = osgx::make_ref<osg::Image>();

		image->readPixels(
			static_cast<int>(viewport->x()),
			static_cast<int>(viewport->y()),
			static_cast<int>(viewport->width()),
			static_cast<int>(viewport->height()),
			GL_RGB,
			GL_UNSIGNED_BYTE
		);
		_success.store(osgDB::writeImageFile(*image, _filename), std::memory_order_relaxed);
		_done.store(true, std::memory_order_release);

		if(_success.load(std::memory_order_relaxed)) {
			OSG_NOTICE << "Wrote framebuffer screenshot: " << _filename << std::endl;
		}

		else OSG_WARN << "Failed to write framebuffer screenshot: " << _filename << std::endl;
	}

	bool done() const {
		return _done.load(std::memory_order_acquire);
	}

	bool success() const {
		return _success.load(std::memory_order_relaxed);
	}

private:
	osg::observer_ptr<osg::Camera> _camera;
	std::string _filename;
	mutable std::atomic<bool> _started{false};
	mutable std::atomic<bool> _done{false};
	mutable std::atomic<bool> _success{false};
};

void applyTemporaryKhronosCamera(osg::Camera* camera) {
	// TEMPORARY BoomBox parity fixture copied exactly from ~/tmp/khronos/BoomBox.gltf.
	// The Khronos export is Y-up; osgx's glTF scene is Z-up.
	const osg::Vec3d eye(0.0, -0.041496723890304565, 0.0);
	const osg::Vec3d forward(0.0, 1.0, 0.0);
	const osg::Vec3d up(0.0, 0.0, 1.0);

	// DamagedHelmet fixture from ~/Downloads/camera.gltf, retained for the next comparison:
	// const osg::Vec3d eye(
	// 	-0.0024815797805786133,
	// 	-3.783294677734375,
	// 	0.00001055002212524414
	// );
	// znear = 0.003815311004049344
	// zfar = 38.15311004049344

	camera->setViewMatrixAsLookAt(eye, eye + forward, up);
	camera->setProjectionMatrixAsPerspective(
		45.0,
		1.5219721329046088,
		0.000039875311734142786,
		0.3987531173414279
	);
}

}

int main(int argc, char** argv) {
	// Khronos requests an antialiased WebGL2 context. WebGL leaves the exact sample count to the
	// browser; the desktop path used for these parity captures ordinarily resolves to 4x MSAA.
	osg::DisplaySettings::instance()->setNumMultiSamples(4);

	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	args.getApplicationUsage()->setCommandLineUsage(
		std::string(args.getApplicationName()) +
		" <model.gltf> --ktx2 <path> --hdr <path> [--capture <path.png>] [--debug [mode]]"
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
		"--capture <path.png>",
		"Write the first complete framebuffer to a PNG and exit"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--debug [mode]",
		"combined, diffuse, specular, base-color, roughness, metallic, normal-texture, normal-texture-raw, geometry-normal, shading-normal, geometry-tangent, bitangent, linear-diffuse, linear-specular, or linear-combined"
	);

	std::string ktx2Path, hdrPath, capturePath, debugName = "combined";
	const std::map<std::string, int> debugModes = {
		{"combined", 0}, {"diffuse", 1}, {"specular", 2},
		{"base-color", 3}, {"roughness", 4}, {"metallic", 5},
		{"normal-texture", 6}, {"normal-texture-raw", 7}, {"geometry-normal", 8},
		{"shading-normal", 9}, {"geometry-tangent", 10}, {"bitangent", 11},
		{"linear-diffuse", 12}, {"linear-specular", 13}, {"linear-combined", 14}
	};

	const bool haveKtx2 = args.read("--ktx2", ktx2Path);
	const bool haveHdr = args.read("--hdr", hdrPath);
	const bool captureRequested = args.read("--capture", capturePath);
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
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.getCamera()->setClearColor(osg::Vec4f(
		48.0f / 255.0f,
		53.0f / 255.0f,
		66.0f / 255.0f,
		1.0f
	)); // #303542
	applyTemporaryKhronosCamera(viewer.getCamera());

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

	osg::ref_ptr<FramebufferPNG> capture;

	if(captureRequested) {
		capture = new FramebufferPNG(viewer.getCamera(), capturePath);
		viewer.getCamera()->setFinalDrawCallback(capture);
	}

	while(!viewer.done()) {
		viewer.frame();

		if(capture && capture->done()) break;
	}

	return capture && !capture->success() ? 1 : 0;
}
