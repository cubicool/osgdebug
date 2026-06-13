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
// TODO(optimization): replace the full W×H pick FBO with a tiny NxN FBO + sub-frustum
//   projection matrix ("pick matrix", a la gluPickMatrix) so the GPU only rasterizes
//   the region under the cursor. See memory: picking_subfrust_todo.md.
//
// TODO(hover): forward MOVE events in PickHandler to enable continuous per-frame picking.
//   The NodeCallback already runs every update traversal; just keep _requested=true.
//
// Scene: five spheres, each with a pickID uniform (1–5). ID 0 = background.
// Left-click anywhere to print the picked object ID.

#include "../osgDebug.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Image>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Uniform>

#include <osgDB/WriteFile>
#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ------------------------------------------------------------------------------------------------
// Pick shaders — hook-based design, multi-shader-object linking
//
// Two separate osg::Shader objects per stage are linked into one osg::Program (spec-legal
// GLSL separate compilation, same pattern as osgSlug). The core shader declares the hook
// function prototype; the hook shader provides the definition. Callers can swap hook shaders
// without recompiling the core. No string concatenation needed.
//
//   pickVertexHook() — called at the end of the vertex stage; forward per-vertex attributes
//                      (e.g. a flat uint aPickID) to the fragment stage here.
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
uniform uint pickID;
uint getPickID() { return pickID; }
)glsl";

inline uint32_t decodePickID(const uint8_t* px) {
    return uint32_t(px[0]) | (uint32_t(px[1]) << 8) | (uint32_t(px[2]) << 16);
}

// ------------------------------------------------------------------------------------------------
// Pick rules — called after readback with the raw NxN RGBA pixel buffer.
// Signature: uint32_t(const uint8_t* pixels, int n)
//   pixels — row-major RGBA, n*n pixels, Y=0 at bottom-left (OpenGL / glReadPixels convention)
//   n      — side length of the pick region; n=1 is the degenerate single-pixel case
// ------------------------------------------------------------------------------------------------

using PickRule = std::function<uint32_t(const uint8_t*, int)>;

// Center pixel wins — semantics identical to n=1; larger n just widens the rasterized region.
inline uint32_t pickCenter(const uint8_t* px, int n) {
    int half   = n / 2;
    int offset = (half * n + half) * 4;
    return decodePickID(px + offset);
}

// Most-covered non-zero ID wins — useful in dense or overlapping scenes.
inline uint32_t pickMostCoverage(const uint8_t* px, int n) {
    std::unordered_map<uint32_t, int> counts;

    for(int i = 0; i < n * n; i++) {
        uint32_t id = decodePickID(px + i * 4);
        if(id) counts[id]++;
    }

    if(counts.empty()) return 0;

    return std::max_element(counts.begin(), counts.end(),
        [](const auto& a, const auto& b){ return a.second < b.second; })->first;
}

// Non-zero ID nearest to center wins — good for snap-to-object / hover picking.
inline uint32_t pickNearestToCenter(const uint8_t* px, int n) {
    int      half   = n / 2;
    int      bestD  = n * n + 1;   // larger than any possible squared distance in the grid
    uint32_t bestID = 0;

    for(int row = 0; row < n; row++) {
        for(int col = 0; col < n; col++) {
            uint32_t id = decodePickID(px + (row * n + col) * 4);
            if(!id) continue;
            int dx = col - half, dy = row - half;
            int d  = dx * dx + dy * dy;
            if(d < bestD) { bestD = d; bestID = id; }
        }
    }

    return bestID;
}

// ------------------------------------------------------------------------------------------------
// Readback: osg::NodeCallback that reads from OSG's built-in image attachment
//
// OSG's cam->attach(image) reads the FBO into the image inside RenderStage::drawImplementation
// while the FBO is still bound. We sample image->data() one frame later in the update
// traversal — invisible latency for click-only picking.
//
// Two modes:
//   CLICK      — reads only when requestPick() is called; for full and small-pick FBOs.
//   CONTINUOUS — reads every frame from pixel [0,0]; for the 1×1 sub-frustum FBO where
//                the projection tracks the cursor continuously.
//
// winW / winH are the actual window dimensions. When the pick image is smaller (small-pick),
// mouse coords are scaled from window space to image space before sampling.
// ------------------------------------------------------------------------------------------------

class PickReadback : public osg::NodeCallback {
public:
    enum class Mode { CLICK, CONTINUOUS };

    PickReadback(int pickSize, PickRule rule, osg::Image* image, int winW, int winH,
                 Mode mode = Mode::CLICK)
        : _pickSize(pickSize), _rule(std::move(rule)),
          _winW(winW), _winH(winH), _mode(mode), _image(image) {}

    // Called from the event thread — safe from any thread.
    void requestPick(int x, int y) {
        _x.store(x, std::memory_order_relaxed);
        _y.store(y, std::memory_order_relaxed);
        _requested.store(true, std::memory_order_release);
    }

    // Continuous mode: update the cursor position every MOVE event so the matrix
    // sync callback can build the sub-frustum projection.
    void updateMouse(int x, int y) {
        _x.store(x, std::memory_order_relaxed);
        _y.store(y, std::memory_order_relaxed);
    }

    int      mouseX() const { return _x.load(std::memory_order_relaxed); }
    int      mouseY() const { return _y.load(std::memory_order_relaxed); }
    uint32_t lastID() const { return _lastID.load(std::memory_order_acquire); }

    void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
        bool doRead = (_mode == Mode::CONTINUOUS) ||
                      _requested.exchange(false, std::memory_order_acq_rel);

        if(doRead) {
            int imgW = _image->s();
            int imgH = _image->t();
            const uint8_t* data = _image->data();

            if(data) {
                int imgX, imgY;

                if(_mode == Mode::CONTINUOUS) {
                    // 1×1 FBO: the single pixel is always at [0,0].
                    imgX = imgY = 0;
                } else {
                    // Scale window coords → image coords (no-op when sizes match).
                    int wx = _x.load(std::memory_order_relaxed);
                    int wy = _y.load(std::memory_order_relaxed);
                    imgX = (imgW == _winW) ? wx : wx * imgW / _winW;
                    imgY = (imgH == _winH) ? wy : wy * imgH / _winH;
                    imgX = std::clamp(imgX, 0, imgW - 1);
                    imgY = std::clamp(imgY, 0, imgH - 1);
                }

                int N  = _pickSize;
                int cx = std::clamp(imgX, N/2, imgW - (N+1)/2);
                int cy = std::clamp(imgY, N/2, imgH - (N+1)/2);

                std::vector<uint8_t> region(static_cast<std::size_t>(N * N * 4));

                for(int row = 0; row < N; row++) {
                    for(int col = 0; col < N; col++) {
                        int srcIdx = ((cy - N/2 + row) * imgW + (cx - N/2 + col)) * 4;
                        int dstIdx = (row * N + col) * 4;
                        std::copy_n(data + srcIdx, 4, region.data() + dstIdx);
                    }
                }

                uint32_t id = _rule(region.data(), N);
                _lastID.store(id, std::memory_order_release);

                if(_mode != Mode::CONTINUOUS) {
                    int wx = _x.load(std::memory_order_relaxed);
                    int wy = _y.load(std::memory_order_relaxed);
                    OSG_NOTICE << "Pick (" << wx << ", " << wy << ") -> ID " << id << std::endl;
                }
            }
        }

        traverse(node, nv);
    }

private:
    int      _pickSize;
    PickRule _rule;
    int      _winW, _winH;
    Mode     _mode;

    osg::ref_ptr<osg::Image>      _image;
    mutable std::atomic<int>      _x{0}, _y{0};
    mutable std::atomic<bool>     _requested{false};
    mutable std::atomic<uint32_t> _lastID{0};
};

// ------------------------------------------------------------------------------------------------
// Event handler: forwards left-clicks to the active readback callback
// ------------------------------------------------------------------------------------------------

class PickHandler : public osgGA::GUIEventHandler {
public:
    // continuous=true: forward MOVE events for sub-frustum tracking; left-click queries lastID.
    // continuous=false: left-click triggers a one-shot pick read.
    explicit PickHandler(PickReadback* rb, bool continuous = false)
        : _rb(rb), _continuous(continuous) {}

    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
        int x = static_cast<int>(ea.getX());
        int y = static_cast<int>(ea.getY());

        if(ea.getEventType() == osgGA::GUIEventAdapter::MOVE && _continuous) {
            _rb->updateMouse(x, y);
            return false;
        }

        if(ea.getEventType() == osgGA::GUIEventAdapter::PUSH &&
           ea.getButton()    == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) {
            if(_continuous) {
                OSG_NOTICE << "Pick (" << x << ", " << y << ") -> ID "
                           << _rb->lastID() << std::endl;
            } else {
                OSG_NOTICE << "Click (" << x << ", " << y << ")" << std::endl;
                _rb->requestPick(x, y);
            }
        }

        return false;
    }

private:
    osg::ref_ptr<PickReadback> _rb;
    bool _continuous;
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
    osg::Image* image,
    osg::Shader* vertHook = nullptr,
    osg::Shader* fragHook = nullptr
) {
    auto cam = osgx::make_ref<osg::Camera>();
    cam->setName("PickCamera");
    cam->setRenderOrder(osg::Camera::POST_RENDER);
    cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    cam->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    cam->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f));   // black = ID 0 = no pick
    cam->setViewport(0, 0, w, h);
    // Without ABSOLUTE_RF the camera composes view/projection with the parent
    // transform stack, producing a wrong cull frustum that clips all geometry.
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->attach(osg::Camera::COLOR_BUFFER, image);

    // Two separate osg::Shader objects per stage — spec-legal GLSL separate compilation.
    // Core declares the hook prototype; hook provides the definition. Previously done via
    // string concatenation as a workaround, but the real breakage was the missing
    // setReferenceFrame(ABSOLUTE_RF), now fixed.
    auto prog = osgx::make_ref<osg::Program>();
    prog->setName("pickProgram");

    auto* vc = new osg::Shader(osg::Shader::VERTEX,   PICK_VERT_CORE);
    auto* vh = vertHook ? vertHook : new osg::Shader(osg::Shader::VERTEX,   PICK_VERT_NOOP);
    auto* fc = new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_CORE);
    auto* fh = fragHook ? fragHook : new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_DEFAULT);

    vc->setName("pickVertCore");  vh->setName("pickVertHook");
    fc->setName("pickFragCore");  fh->setName("pickFragHook");

    prog->addShader(vc);  prog->addShader(vh);
    prog->addShader(fc);  prog->addShader(fh);

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

    auto mode = pick1x1 ? PickReadback::Mode::CONTINUOUS : PickReadback::Mode::CLICK;

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
    auto pickCam = createPickCamera(pickW, pickH, pickImage.get());
    pickCam->addChild(scene);

    auto rb = osgx::make_ref<PickReadback>(pickSize, pickCenter, pickImage.get(), W, H, mode);
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

    viewer.addEventHandler(new PickHandler(rb, pick1x1));
    viewer.setSceneData(root);

    return viewer.run();
}
