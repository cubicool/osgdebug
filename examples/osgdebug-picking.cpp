// vimrun! ./examples/osgdebug-picking
//
// Texture-based (object ID) picking with two readback modes:
//
//   SYNC  (default): glReadPixels immediately in the postDrawCallback — simplest,
//                    stalls the pipeline for the one pixel read. Fine for click-only picking.
//
//   ASYNC (--async): double-buffered PBO readback — issues DMA on frame N, maps the
//                    OTHER PBO on frame N+1. Zero meaningful stall; one-frame lag that
//                    ring-buffer averaging (or hover semantics) makes irrelevant.
//
// The pattern mirrors the SYNC/ASYNC split in osgDebug::FinalDrawCallback (timer queries).
//
// Scene: five spheres, each with a pickID uniform (1–5). ID 0 = background.
// Left-click anywhere to print the picked object ID.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Texture2D>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Uniform>

#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <cstdint>

// ------------------------------------------------------------------------------------------------
// Pick shaders — hook-based design
//
// Core shaders declare two hook functions that the user can override by supplying additional
// shader objects to createPickCamera(). OSG links all shaders of the same stage together, so
// the user's definition satisfies the prototype declared here.
//
//   pickVertexHook() — called at the end of the vertex stage; use it to forward per-vertex
//                      attributes (e.g. a flat uint aPickID) to the fragment stage.
//                      Default: no-op (PICK_VERT_NOOP).
//
//   getPickID()      — called by the fragment stage; must return the pick ID for this fragment.
//                      Default: reads uniform uint pickID (PICK_FRAG_DEFAULT).
//
// The pick camera installs the assembled Program with OVERRIDE so it wins over every per-object
// shader. Per-object uniforms (e.g. pickID) are still visible via the inherited state stack.
// ------------------------------------------------------------------------------------------------

static const char* PICK_VERT_CORE = R"glsl(
#version 330 core
in vec4 osg_Vertex;
uniform mat4 osg_ModelViewProjectionMatrix;
void pickVertexHook();
void main() {
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
    pickVertexHook();
}
)glsl";

static const char* PICK_VERT_NOOP = R"glsl(
#version 330 core
void pickVertexHook() {}
)glsl";

// Encode a 24-bit ID into RGB. Alpha is always 1 so the FBO clear (ID 0 → black) is distinct.
static const char* PICK_FRAG_CORE = R"glsl(
#version 330 core
out vec4 fragColor;
uint getPickID();
void main() {
    uint id = getPickID();
    fragColor = vec4(
        float( id         & 0xFFu) / 255.0,
        float((id >>  8u) & 0xFFu) / 255.0,
        float((id >> 16u) & 0xFFu) / 255.0,
        1.0
    );
}
)glsl";

static const char* PICK_FRAG_DEFAULT = R"glsl(
#version 330 core
uniform uint pickID;
uint getPickID() { return pickID; }
)glsl";

inline uint32_t decodePickID(const uint8_t* px) {
    return uint32_t(px[0]) | (uint32_t(px[1]) << 8) | (uint32_t(px[2]) << 16);
}

// ------------------------------------------------------------------------------------------------
// Shared base: atomic mouse coords + result storage
// ------------------------------------------------------------------------------------------------

class PickReadbackBase : public osg::Camera::DrawCallback {
public:
    void requestPick(int x, int y) {
        _x.store(x, std::memory_order_relaxed);
        _y.store(y, std::memory_order_relaxed);
        _requested.store(true, std::memory_order_release);
    }

    uint32_t lastID() const { return _lastID.load(std::memory_order_acquire); }

protected:
    mutable std::atomic<int>      _x{0}, _y{0};
    mutable std::atomic<bool>     _requested{false};
    mutable std::atomic<uint32_t> _lastID{0};
};

// ------------------------------------------------------------------------------------------------
// SYNC readback
// ------------------------------------------------------------------------------------------------

class SyncReadback : public PickReadbackBase {
public:
    void operator()(osg::RenderInfo&) const override {
        if(!_requested.exchange(false, std::memory_order_acq_rel)) return;

        int x = _x.load(std::memory_order_relaxed);
        int y = _y.load(std::memory_order_relaxed);

        // The pick camera's FBO is still bound here — glReadPixels reads from it directly.
        // Y=0 is bottom-left in both OSG events and OpenGL, so no flip is needed for a
        // standard OSG viewer. If your window system inverts Y, flip: (viewportH - 1 - y).
        uint8_t px[4]{};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

        uint32_t id = decodePickID(px);
        _lastID.store(id, std::memory_order_release);

        OSG_NOTICE << "SYNC pick (" << x << ", " << y << ") -> ID " << id << std::endl;
    }
};

// ------------------------------------------------------------------------------------------------
// ASYNC readback: double-buffered PBO, one-frame lag
//
// Every frame while _inFlight:
//   1. Map PBO[1-idx] (written last frame) → read result (GPU already done)
//   2. Issue glReadPixels into PBO[idx] → DMA starts, returns immediately
//   3. Flip idx
//
// A new requestPick() updates the stored coordinates; the readback continues running
// at the stored position until you request again (suitable for hover picking too).
// ------------------------------------------------------------------------------------------------

class AsyncReadback : public PickReadbackBase {
public:
    void operator()(osg::RenderInfo& ri) const override {
        auto* ext = ri.getState()->get<osg::GLExtensions>();

        if(!_init) {
            ext->glGenBuffers(2, _pbos);

            for(int i = 0; i < 2; i++) {
                ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbos[i]);
                // 4 bytes = one RGBA pixel
                ext->glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
            }

            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            _init = true;
        }

        // --- Phase 1: read result from the PBO written last frame ---
        if(_inFlight) {
            int readIdx = 1 - _idx;

            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbos[readIdx]);

            auto* ptr = static_cast<uint8_t*>(
                ext->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)
            );

            if(ptr) {
                uint32_t id = decodePickID(ptr);
                _lastID.store(id, std::memory_order_release);
                ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

                OSG_NOTICE
                    << "ASYNC pick (" << _pickX << ", " << _pickY << ") -> ID " << id
                    << std::endl;
            }

            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }

        // --- Accept a new pick request ---
        if(_requested.exchange(false, std::memory_order_acq_rel)) {
            _pickX   = _x.load(std::memory_order_relaxed);
            _pickY   = _y.load(std::memory_order_relaxed);
            _inFlight = true;
        }

        // --- Phase 2: issue DMA into this frame's PBO ---
        if(_inFlight) {
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbos[_idx]);
            glReadPixels(_pickX, _pickY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            _idx ^= 1;
        }
    }

private:
    mutable GLuint _pbos[2]{0, 0};
    mutable int    _idx{0};
    mutable bool   _init{false};
    mutable bool   _inFlight{false};
    mutable int    _pickX{0}, _pickY{0};
};

// ------------------------------------------------------------------------------------------------
// Event handler: forwards left-clicks to the active readback callback
// ------------------------------------------------------------------------------------------------

class PickHandler : public osgGA::GUIEventHandler {
public:
    explicit PickHandler(PickReadbackBase* rb) : _rb(rb) {}

    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
        if(
            ea.getEventType() == osgGA::GUIEventAdapter::PUSH &&
            ea.getButton()    == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON
        ) {
            int x = static_cast<int>(ea.getX());
            int y = static_cast<int>(ea.getY());

            OSG_NOTICE << "Click (" << x << ", " << y << ")" << std::endl;

            _rb->requestPick(x, y);
        }

        return false;
    }

private:
    osg::ref_ptr<PickReadbackBase> _rb;
};

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
// Pick camera: RTT FBO, same resolution as the window, pick shader with OVERRIDE
//
// vertHook — optional vertex shader defining pickVertexHook(); defaults to a no-op.
//            Provide one to forward per-vertex attributes (e.g. a flat uint) to the frag stage.
// fragHook — optional fragment shader defining getPickID(); defaults to reading uniform uint pickID.
//            Provide one to source the pick ID from a vertex attribute, texture, or anything else.
// ------------------------------------------------------------------------------------------------

osg::ref_ptr<osg::Camera> createPickCamera(
    int w, int h,
    osg::Shader* vertHook = nullptr,
    osg::Shader* fragHook = nullptr
) {
    auto cam = osgx::make_ref<osg::Camera>();
    cam->setName("PickCamera");
    cam->setRenderOrder(osg::Camera::PRE_RENDER);
    cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    cam->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    cam->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f));   // black = ID 0 = no pick
    cam->setViewport(0, 0, w, h);

    // NEAREST filter: no interpolation between IDs, which would corrupt the encoding
    auto tex = osgx::make_ref<osg::Texture2D>();
    tex->setTextureSize(w, h);
    tex->setInternalFormat(GL_RGBA);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
    cam->attach(osg::Camera::COLOR_BUFFER, tex);

    auto prog = osgx::make_ref<osg::Program>();
    prog->setName("pickProgram");

    prog->addShader(new osg::Shader(osg::Shader::VERTEX,   PICK_VERT_CORE));
    prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_CORE));

    prog->addShader(vertHook ? vertHook : new osg::Shader(osg::Shader::VERTEX,   PICK_VERT_NOOP));
    prog->addShader(fragHook ? fragHook : new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_DEFAULT));

    auto* ss = cam->getOrCreateStateSet();

    ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

    // Blending and dithering both corrupt ID colors — disable for the pick pass
    ss->setMode(GL_BLEND,  osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    ss->setMode(GL_DITHER, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

    return cam;
}

// ------------------------------------------------------------------------------------------------
// main
// ------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    osg::ArgumentParser args(&argc, argv);

    bool useAsync = args.read("--async");

    OSG_NOTICE
        << "Pick mode: "
        << (useAsync ? "ASYNC (PBO, 1-frame lag)" : "SYNC (glReadPixels, immediate)")
        << " — left-click to pick"
        << std::endl;

    osgViewer::Viewer viewer(args);
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.addEventHandler(new osgViewer::StatsHandler());

    // Realize first so we can query the actual viewport size
    viewer.realize();

    auto* vp = viewer.getCamera()->getViewport();
    int W = static_cast<int>(vp->width());
    int H = static_cast<int>(vp->height());

    OSG_NOTICE << "Viewport: " << W << "x" << H << std::endl;

    auto scene    = createScene();
    auto pickCam  = createPickCamera(W, H);

    // Pick camera renders the same scene node (shared ref_ptr)
    pickCam->addChild(scene);

    // Sync pick camera's view/projection to the viewer's main camera every update traversal.
    // The pick camera is PRE_RENDER, so it uses whatever matrices we set here before it draws.
    auto root = osgx::make_ref<osg::Group>();
    root->setName("root");

    root->setUpdateCallback(new osgx::NodeLambdaCallback(
        [&viewer, pc = pickCam.get()](osg::Node* n, osg::NodeVisitor* nv) {
            pc->setViewMatrix(viewer.getCamera()->getViewMatrix());
            pc->setProjectionMatrix(viewer.getCamera()->getProjectionMatrix());
            n->traverse(*nv);
        }
    ));

    root->addChild(pickCam);
    root->addChild(scene);

    // Build and wire up the readback callback
    osg::ref_ptr<PickReadbackBase> rb;

    if(useAsync) rb = osgx::make_ref<AsyncReadback>();
    else         rb = osgx::make_ref<SyncReadback>();

    pickCam->setPostDrawCallback(rb);

    viewer.addEventHandler(new PickHandler(rb));
    viewer.setSceneData(root);

    return viewer.run();
}
