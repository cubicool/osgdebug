// vimrun! ./examples/osgdebug-picking
//
// Texture-based (object ID) picking via RTT FBO + osg::Image readback.
//
// Readback: OSG reads the pick FBO into an osg::Image inside RenderStage::drawImplementation
// while the FBO is still bound. PickReadback (an update NodeCallback) samples image->data()
// one frame later — invisible latency for click-only picking.
//
// --pick-size N (default 1): sample an NxN region centered on the cursor.
//   N=1 is pixel-perfect. N=3/5/… adds a tolerance zone for thin geometry.
//   PickRule selects one ID from the NxN buffer (pickCenter by default;
//   pickMostCoverage and pickNearestToCenter also provided).
//
// Scene: five spheres, each with a pickID uniform (1–5). ID 0 = background.
// Left-click anywhere to print the picked object ID.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osg/Uniform>

#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

// osgx::makePickCamera, osgx::PickReadback, osgx::PickHandler, osgx::PickRule, pick rules,
// and all pick shader core strings live in osgx.hpp.
// This example supplies only the scene and the wiring (matrix sync, FBO sizing, args).

// ------------------------------------------------------------------------------------------------
// Scene: five colored spheres, each with a pickID uniform (1–5)
// ------------------------------------------------------------------------------------------------

osg::ref_ptr<osg::Group> createScene() {
    auto root = osgx::make_ref<osg::Group>();
    root->setName("scene");

    struct Entry { osg::Vec3 pos; osg::Vec4 color; };

    static const Entry OBJECTS[] = {
        { { -8.0f, 0.0f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f } },   // ID 1 — red
        { { -4.0f, 0.0f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f } },   // ID 2 — green
        { {  0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 1.0f, 1.0f } },   // ID 3 — blue
        { {  4.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.2f, 1.0f } },   // ID 4 — yellow
        { {  8.0f, 0.0f, 0.0f }, { 1.0f, 0.2f, 1.0f, 1.0f } },   // ID 5 — magenta
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

    int  pickSize  = 1;
    int  smallPick = 0;
    bool pick1x1   = args.read("--pick-1x1");
    args.read("--pick-size",  pickSize);
    args.read("--small-pick", smallPick);
    if(pickSize < 1) pickSize = 1;

    osgViewer::Viewer viewer(args);
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.addEventHandler(new osgViewer::StatsHandler());

    viewer.realize();

    auto* vp = viewer.getCamera()->getViewport();
    int W = static_cast<int>(vp->width());
    int H = static_cast<int>(vp->height());

    // Determine pick FBO dimensions.
    int pickW, pickH;
    if(pick1x1) {
        pickW = pickH = 1;
    } else if(smallPick > 0) {
        pickW = pickH = smallPick;
    } else {
        pickW = W;  pickH = H;
    }

    auto mode = pick1x1 ? osgx::PickReadback::Mode::CONTINUOUS : osgx::PickReadback::Mode::CLICK;

    OSG_NOTICE << "Pick FBO: " << pickW << "x" << pickH
               << "  window: " << W << "x" << H
               << (pick1x1   ? "  mode: 1x1 sub-frustum (continuous)"  :
                   smallPick ? "  mode: small-pick (scaled coords)"     :
                               "  mode: full FBO")
               << "  region: " << pickSize << "x" << pickSize
               << " — left-click to pick"
               << std::endl;

    auto pickImage = osgx::make_ref<osg::Image>();
    pickImage->allocateImage(pickW, pickH, 1, GL_RGBA, GL_UNSIGNED_BYTE);

    auto scene   = createScene();
    auto pickCam = osgx::makePickCamera(pickW, pickH, pickImage.get());
    pickCam->addChild(scene);

    auto rb = osgx::make_ref<osgx::PickReadback>(pickSize, osgx::pickCenter, pickImage.get(), W, H, mode);
    pickCam->setUpdateCallback(rb);

    auto root = osgx::make_ref<osg::Group>();
    root->setName("root");

    // Sync view matrix; for 1×1 mode also build a sub-frustum projection centered on
    // the cursor (equivalent to gluPickMatrix * viewerProjection).
    root->setUpdateCallback(new osgx::NodeLambdaCallback(
        [&viewer, pc = pickCam.get(), rb = rb.get(), pick1x1, W, H]
        (osg::Node* n, osg::NodeVisitor* nv) {
            pc->setViewMatrix(viewer.getCamera()->getViewMatrix());

            if(pick1x1) {
                // Sub-frustum: maps the 1×1 pixel at the cursor to fill the entire NDC cube.
                // Equivalent to prepending gluPickMatrix(cx+0.5, cy+0.5, 1, 1, [0,0,W,H])
                // to the projection matrix (column-vector OpenGL convention → row-vector OSG).
                double cx = rb->mouseX() + 0.5;
                double cy = rb->mouseY() + 0.5;
                osg::Matrix pickMat(
                    W,          0, 0, 0,
                    0,          H, 0, 0,
                    0,          0, 1, 0,
                    W - 2.0*cx, H - 2.0*cy, 0, 1
                );
                pc->setProjectionMatrix(viewer.getCamera()->getProjectionMatrix() * pickMat);
            } else {
                pc->setProjectionMatrix(viewer.getCamera()->getProjectionMatrix());
            }

            n->traverse(*nv);
        }
    ));

    root->addChild(pickCam);
    root->addChild(scene);

    viewer.addEventHandler(new osgx::PickHandler(rb, pick1x1));
    viewer.setSceneData(root);

    return viewer.run();
}
