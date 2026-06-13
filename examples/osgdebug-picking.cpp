// vimrun! ./examples/osgdebug-picking
//
// Texture-based (object ID) picking via RTT FBO, with two readback modes:
//
//   SYNC  (default): osg::Image attachment -- OSG calls glReadPixels internally during
//                    RenderStage::drawImplementation while the FBO is still bound.
//                    PickReadback (NodeCallback) samples image->data() one frame later.
//
//   ASYNC (--async): double-buffered PBO -- postDrawCallback issues glReadPixels into
//                    PBO[n] (DMA, returns immediately); next frame maps PBO[1-n] for the
//                    result. Zero pipeline stall in steady state; one-frame lag.
//                    NOTE: whether postDrawCallback fires while the pick FBO is still
//                    bound is under test. If IDs always read as 0, the FBO has already
//                    been unbound at that point and we need a different hook.
//
// The SYNC/ASYNC split mirrors the pattern in osgDebug::FinalDrawCallback (timer queries).
//
// --pick-size N     (default 1): sample NxN region centered on cursor (SYNC only).
// --small-pick N    : fixed NxN FBO; mouse coords scaled on the way in (SYNC only).
// --pick-1x1        : 1x1 FBO + sub-frustum projection, continuous hover (SYNC only).
// --async           : Texture2D attachment + PBO glGetTexImage, click mode, 1-frame lag.
//
// Scene: five spheres, each with a pickID uniform (1-5). ID 0 = background.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osg/Uniform>
#include <osg/GLExtensions>

#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

// osgx::makePickCamera, osgx::PickReadbackBase, osgx::PickReadback, osgx::PickHandler,
// osgx::PickRule, pick rules, and all pick shader strings live in osgx.hpp.

// ------------------------------------------------------------------------------------------------
// ASYNC readback: Camera::DrawCallback using a Texture2D attachment + PBO glGetTexImage.
//
// Why Texture2D instead of osg::Image:
//   - Image attachment: OSG calls glReadPixels to CPU memory every frame inside
//     drawImplementation (blocking); postDrawCallback fires after FBO unbind.
//   - Texture2D attachment: OSG uses glFramebufferTexture2D (renders directly into the
//     texture, no automatic CPU readback). After FBO unbind, the texture has the data.
//     glGetTexImage reads from the texture object -- FBO binding is irrelevant.
//     With a PBO bound, glGetTexImage is async DMA; the callback returns immediately.
//
// Click-triggered only: glGetTexImage is issued once per requestPick(), not every frame.
// The full W×H texture is downloaded per click; the specific pixel is extracted when
// mapping the PBO one frame later.
//
// Frame N (requestPick fired): postDrawCallback issues glGetTexImage into PBO (async DMA).
// Frame N+1 postDrawCallback: maps PBO (GPU done -- one frame elapsed, no stall),
//   extracts pixel at (_pickX, _pickY), stores _lastID.
// ------------------------------------------------------------------------------------------------

class AsyncReadback : public osgx::PickReadbackBase, public osg::Camera::DrawCallback {
public:
    enum class Mode { CLICK, CONTINUOUS };

    AsyncReadback(osg::Texture2D* tex, int imgW, int imgH, Mode mode = Mode::CLICK):
    _tex(tex), _imgW(imgW), _imgH(imgH), _mode(mode) {}

    void operator()(osg::RenderInfo& ri) const override {
        auto& state = *ri.getState();
        auto* ext = state.get<osg::GLExtensions>();
        std::size_t bufSize = static_cast<std::size_t>(_imgW * _imgH * 4);

        if(!_init) {
            ext->glGenBuffers(1, &_pbo);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
            ext->glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufSize), nullptr, GL_STREAM_READ);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            _init = true;
        }

        // Phase 1: map and decode the previous frame's download (DMA is complete).
        if(_inFlight) {
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);

            auto* ptr = static_cast<const uint8_t*>(
                ext->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)
            );

            if(ptr) {
                int px = std::clamp(_pickX, 0, _imgW - 1);
                int py = std::clamp(_pickY, 0, _imgH - 1);
                uint32_t id = osgx::decodePickID(ptr + (py * _imgW + px) * 4);
                _lastID.store(id, std::memory_order_release);

                if(_mode == Mode::CLICK) {
                    OSG_NOTICE
                        << "ASYNC pick (" << _pickX << ", " << _pickY
                        << ") -> ID " << id
                        << std::endl;
                } else if(id != _prevID) {
                    // CONTINUOUS: print only when hovering over a new object (not background).
                    _prevID = id;
                    if(id != 0) {
                        OSG_NOTICE << "Hover -> ID " << id << std::endl;
                    }
                }

                ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }

            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            _inFlight = false;
        }

        // Phase 2: issue async texture download.
        // CLICK: only when explicitly requested.
        // CONTINUOUS: every frame (1x1 texture = 4 bytes, negligible cost).
        bool doDownload =
            (_mode == Mode::CONTINUOUS) ||
            _requested.exchange(false, std::memory_order_acq_rel);

        if(doDownload) {
            _pickX = _x.load(std::memory_order_relaxed);
            _pickY = _y.load(std::memory_order_relaxed);

            // apply() binds the texture and ensures it exists in this GL context
            // (matches osgEarth TileRasterizer pattern).
            _tex->apply(state);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            _inFlight = true;
        }
    }

private:
    osg::ref_ptr<osg::Texture2D> _tex;
    int _imgW, _imgH;
    Mode _mode;

    mutable GLuint   _pbo{0};
    mutable bool     _init{false};
    mutable bool     _inFlight{false};
    mutable int      _pickX{0}, _pickY{0};
    mutable uint32_t _prevID{0};
};

// ------------------------------------------------------------------------------------------------
// Scene: five colored spheres, each with a pickID uniform (1-5)
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

    bool useAsync = args.read("--async");
    bool pick1x1  = args.read("--pick-1x1");
    int  pickSize  = 1;
    int  smallPick = 0;
    args.read("--pick-size",  pickSize);
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

    if(pick1x1) {
        pickW = pickH = 1;
    } else if(smallPick > 0) {
        pickW = pickH = smallPick;
    } else {
        pickW = W; pickH = H;
    }

    OSG_NOTICE << "Pick FBO: " << pickW << "x" << pickH
               << "  window: " << W << "x" << H << "\n"
               << "  readback: "
               << (useAsync && pick1x1  ? "ASYNC (Texture2D + PBO, 1x1 sub-frustum, continuous hover)" :
                   useAsync            ? "ASYNC (Texture2D + PBO, click)"                            :
                   pick1x1             ? "SYNC (osg::Image, 1x1 sub-frustum, continuous)"            :
                   smallPick           ? "SYNC (osg::Image, small FBO scaled coords)"                :
                                         "SYNC (osg::Image, full FBO)")
               << "\n"
               << "  region: " << pickSize << "x" << pickSize
               << " — left-click to pick"
               << std::endl;

    auto scene = createScene();

    // Wire up the readback. rb is a raw pointer into an object kept alive by the camera.
    osgx::PickReadbackBase* rb = nullptr;
    osg::ref_ptr<osg::Camera> pickCam;

    if(useAsync) {
        // Texture2D attachment: OSG renders directly into the texture (glFramebufferTexture2D),
        // no automatic CPU readback. makePickCamera configures size/format/filters on the texture.
        auto pickTex = osgx::make_ref<osg::Texture2D>();
        pickCam = osgx::makePickCamera(pickW, pickH, pickTex.get());
        pickCam->addChild(scene);

        // pick1x1: 1x1 FBO + sub-frustum = continuous hover; pixel is always (0,0) after clamp.
        // click mode: full-FBO download per click only.
        auto asyncMode = pick1x1 ? AsyncReadback::Mode::CONTINUOUS : AsyncReadback::Mode::CLICK;
        auto asyncRb = osgx::make_ref<AsyncReadback>(pickTex.get(), pickW, pickH, asyncMode);
        pickCam->setPostDrawCallback(asyncRb);
        rb = asyncRb.get();
        viewer.addEventHandler(new osgx::PickHandler(rb, pick1x1));
    } else {
        // Image attachment: OSG calls glReadPixels into image->data() during drawImplementation
        // while the FBO is still bound. PickReadback NodeCallback samples image->data() one frame later.
        auto pickImage = osgx::make_ref<osg::Image>();
        pickImage->allocateImage(pickW, pickH, 1, GL_RGBA, GL_UNSIGNED_BYTE);

        pickCam = osgx::makePickCamera(pickW, pickH, pickImage.get());
        pickCam->addChild(scene);

        auto mode   = pick1x1 ? osgx::PickReadback::Mode::CONTINUOUS : osgx::PickReadback::Mode::CLICK;
        auto syncRb = osgx::make_ref<osgx::PickReadback>(pickSize, osgx::pickCenter, pickImage.get(), W, H, mode);
        pickCam->setUpdateCallback(syncRb);
        rb = syncRb.get();
        viewer.addEventHandler(new osgx::PickHandler(rb, pick1x1));
    }

    auto root = osgx::make_ref<osg::Group>();
    root->setName("root");

    // Sync view matrix; for 1x1 mode also build a sub-frustum projection centered on
    // the cursor (equivalent to gluPickMatrix * viewerProjection).
    root->setUpdateCallback(new osgx::NodeLambdaCallback(
        [&viewer, pc = pickCam.get(), rb, pick1x1, W, H]
        (osg::Node* n, osg::NodeVisitor* nv) {
            pc->setViewMatrix(viewer.getCamera()->getViewMatrix());

            if(pick1x1) {
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

    viewer.setSceneData(root);

    return viewer.run();
}
