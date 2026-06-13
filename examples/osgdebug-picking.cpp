// vimrun! ./examples/osgdebug-picking --pick-1x1 --async
//
// Texture-based (object ID) picking via RTT FBO, with two readback modes:
//
// SYNC (default): osg::Image attachment -- OSG calls glReadPixels internally during
// RenderStage::drawImplementation while the FBO is still bound.
// PickReadbackSync (NodeCallback) samples image->data() one frame later.
//
// ASYNC (--async): Texture2D attachment + PBO glGetTexImage, one-frame lag.
// postDrawCallback fires after FBO unbind; glGetTexImage reads from the texture
// object (FBO binding irrelevant) -- async DMA with a PBO, zero stall.
//
// The SYNC/ASYNC split mirrors the pattern in osgDebug::FinalDrawCallback (timer queries).
//
// --pick-size N (default 1): sample NxN region centered on cursor (SYNC only).
// --small-pick N: fixed NxN FBO; mouse coords scaled on the way in (SYNC only).
// --pick-1x1: 1x1 FBO + sub-frustum projection, continuous hover (SYNC only).
// --async: Texture2D attachment + PBO glGetTexImage, click mode, 1-frame lag.
//
// Scene: five spheres, each with a pickID uniform (1-5). ID 0 = background.
//
// PickReadbackSync, PickReadbackAsync, PickCameraSync, and all pick helpers live in osgx.hpp.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osg/Uniform>

#include <cstring>

#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

// ------------------------------------------------------------------------------------------------
// Scene: five colored spheres, each with a pickID uniform (1-5)
// ------------------------------------------------------------------------------------------------

osg::ref_ptr<osg::Group> createScene() {
	auto root = osgx::make_ref<osg::Group>();

	root->setName("scene");

	struct Entry { osg::Vec3 pos; osg::Vec4 color; };

	static const Entry OBJECTS[] = {
		{{ -8.0f, 0.0f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f }}, // ID 1 -- red
		{{ -4.0f, 0.0f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f }}, // ID 2 -- green
		{{  0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 1.0f, 1.0f }}, // ID 3 -- blue
		{{  4.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.2f, 1.0f }}, // ID 4 -- yellow
		{{  8.0f, 0.0f, 0.0f }, { 1.0f, 0.2f, 1.0f, 1.0f }}, // ID 5 -- magenta
	};

	for(size_t i = 0; i < std::size(OBJECTS); i++) {
		const auto& o = OBJECTS[i];

		auto mt  = osgx::make_ref<osg::MatrixTransform>(osg::Matrix::translate(o.pos));
		auto geo = osgx::make_ref<osg::Geode>();
		auto sd  = osgx::make_ref<osg::ShapeDrawable>(new osg::Sphere(osg::Vec3(), 1.5f));

		sd->setColor(o.color);

		auto* uid = new osg::Uniform(osg::Uniform::UNSIGNED_INT, "pickID");

		uid->set(static_cast<unsigned int>(i + 1));

		geo->getOrCreateStateSet()->addUniform(uid);
		geo->addDrawable(sd);
		mt->addChild(geo);
		root->addChild(mt);
	}

	return root;
}

// ------------------------------------------------------------------------------------------------
// main
// ------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	bool useAsync = args.read("--async");
	bool pick1x1  = args.read("--pick-1x1");
	int  pickSize = 1;
	int  smallPick = 0;

	args.read("--pick-size", pickSize);
	args.read("--small-pick", smallPick);

	if(pickSize < 1) pickSize = 1;

	if(useAsync && smallPick > 0) {
		OSG_WARN << "--async and --small-pick cannot be combined; falling back to SYNC" << std::endl;
		useAsync = false;
	}

	osgViewer::Viewer viewer(args);

	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.realize();

	auto* vp = viewer.getCamera()->getViewport();
	int W = static_cast<int>(vp->width());
	int H = static_cast<int>(vp->height());

	int pickW, pickH;

	if(pick1x1)            { pickW = pickH = 1; }
	else if(smallPick > 0) { pickW = pickH = smallPick; }
	else                   { pickW = W; pickH = H; }

	OSG_NOTICE
		<< "Pick FBO: " << pickW << "x" << pickH
		<< "  window: " << W << "x" << H << "\n"
		<< " readback: "
		<< (
			useAsync && pick1x1 ? "ASYNC (Texture2D + PBO, 1x1 sub-frustum, continuous hover)" :
			useAsync            ? "ASYNC (Texture2D + PBO, click)" :
			pick1x1             ? "SYNC (osg::Image, 1x1 sub-frustum, continuous)" :
			smallPick           ? "SYNC (osg::Image, small FBO scaled coords)" :
			                      "SYNC (osg::Image, full FBO)"
		) << "\n"
		<< "   region: " << pickSize << "x" << pickSize
		<< " -- left-click to pick"
		<< std::endl;

	auto scene = createScene();

	osgx::PickReadback*               rb     = nullptr;
	osg::ref_ptr<osgx::PickReadbackSync> syncRb;
	osg::ref_ptr<osg::Camera>         pickCam;

	if(useAsync) {
		auto pickTex = osgx::make_ref<osg::Texture2D>();

		pickCam = osgx::makePickCamera(pickW, pickH, pickTex.get());
		pickCam->addChild(scene);

		auto mode    = pick1x1 ? osgx::PickReadbackAsync::Mode::CONTINUOUS
		                        : osgx::PickReadbackAsync::Mode::CLICK;
		auto asyncRb = osgx::make_ref<osgx::PickReadbackAsync>(pickTex.get(), pickW, pickH, mode);

		pickCam->setPostDrawCallback(asyncRb);

		rb = asyncRb.get();
	} else {
		auto pickImage = osgx::make_ref<osg::Image>();

		pickImage->allocateImage(pickW, pickH, 1, GL_RGBA, GL_UNSIGNED_BYTE);
		std::memset(pickImage->data(), 0, static_cast<std::size_t>(pickImage->getTotalSizeInBytesIncludingMipmaps()));

		pickCam = osgx::makePickCamera(pickW, pickH, pickImage.get());
		pickCam->addChild(scene);

		auto mode = pick1x1 ? osgx::PickReadbackSync::Mode::CONTINUOUS
		                     : osgx::PickReadbackSync::Mode::CLICK;
		syncRb = osgx::make_ref<osgx::PickReadbackSync>(
			pickSize, osgx::spiralPick, pickImage.get(), W, H, mode
		);

		rb = syncRb.get();
	}

	viewer.addEventHandler(new osgx::PickHandler(rb, pick1x1));

	rb->onPick = [rb, useAsync](uint32_t id, osgx::ActionType type) {
		if(type == osgx::ActionType::HOVER) {
			if(id != 0) OSG_NOTICE << "Hover -> ID " << id << std::endl;
		} else {
			OSG_NOTICE
				<< (useAsync ? "ASYNC pick" : "Pick")
				<< " (" << rb->mouseX() << ", " << rb->mouseY()
				<< ") -> ID " << id << std::endl;
		}
	};

	// PickCameraSync on the pick camera handles view/projection sync every update traversal.
	// In SYNC mode, PickReadbackSync is chained behind it so both run from the same callback slot.
	auto* sync = new osgx::PickCameraSync(viewer.getCamera(), pick1x1, W, H, rb);

	if(syncRb) sync->setNestedCallback(syncRb.get());

	pickCam->setUpdateCallback(sync);

	auto root = osgx::make_ref<osg::Group>();

	root->setName("root");
	root->addChild(pickCam);
	root->addChild(scene);

	viewer.setSceneData(root);

	return viewer.run();
}
