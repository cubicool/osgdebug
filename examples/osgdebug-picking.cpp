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
// The pick FBO is always unbound before any postDrawCallback or finalDrawCallback fires,
// so glReadPixels/glGetTexImage can't be used there reliably. OSG's cam->attach(image)
// mechanism reads the FBO into the image during RenderStage::draw() while the FBO is
// still bound — before the draw callbacks fire. We then read from image->data() in the
// UPDATE traversal (one frame stale), which is invisible for click-only picking on a
// static scene.
//
// No raw GL code needed here at all.
// ------------------------------------------------------------------------------------------------

class PickReadback : public osg::NodeCallback {
public:
    PickReadback(int pickSize, PickRule rule, osg::Image* image)
        : _pickSize(pickSize), _rule(std::move(rule)), _image(image) {}

    void requestPick(int x, int y) {
        _x.store(x, std::memory_order_relaxed);
        _y.store(y, std::memory_order_relaxed);
        _requested.store(true, std::memory_order_release);
    }

    uint32_t lastID() const { return _lastID.load(std::memory_order_acquire); }

    void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
        if(_requested.exchange(false, std::memory_order_acq_rel)) {
            int x = _x.load(std::memory_order_relaxed);
            int y = _y.load(std::memory_order_relaxed);
            int N = _pickSize;
            int W = _image->s();
            int H = _image->t();

            const uint8_t* data = _image->data();

            if(data) {
                // Sample key pixels to diagnose what the pick camera actually rendered.
                // Alpha tells us whether the FBO rendered at all (clear alpha=1 → 255;
                // alpha=0 means the image was never written to).
                auto sample = [&](const char* label, int sx, int sy) {
                    int i = (std::clamp(sy, 0, H-1) * W + std::clamp(sx, 0, W-1)) * 4;
                    OSG_NOTICE << "  " << label
                        << " RGBA(" << (int)data[i+0] << ","
                                    << (int)data[i+1] << ","
                                    << (int)data[i+2] << ","
                                    << (int)data[i+3] << ")\n";
                };

                OSG_NOTICE << "[PICK] image samples:\n";
                sample("click pos",  x,   y);
                sample("[0,0]",      0,   0);
                sample("[center]",   W/2, H/2);
                sample("[W-1,H-1]",  W-1, H-1);

                osgDB::writeImageFile(*_image, "pick_debug.png");
                OSG_NOTICE << "[PICK] wrote pick_debug.png\n";

                int cx = std::clamp(x, N/2, W - (N+1)/2);
                int cy = std::clamp(y, N/2, H - (N+1)/2);

                std::vector<uint8_t> region(static_cast<std::size_t>(N * N * 4));

                for(int row = 0; row < N; row++) {
                    for(int col = 0; col < N; col++) {
                        int srcIdx = ((cy - N/2 + row) * W + (cx - N/2 + col)) * 4;
                        int dstIdx = (row * N + col) * 4;
                        std::copy_n(data + srcIdx, 4, region.data() + dstIdx);
                    }
                }

                uint32_t id = _rule(region.data(), N);
                _lastID.store(id, std::memory_order_release);

                OSG_NOTICE << "Pick (" << x << ", " << y << ") -> ID " << id << std::endl;
            }
        }

        traverse(node, nv);
    }

private:
    int      _pickSize;
    PickRule _rule;

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
    explicit PickHandler(PickReadback* rb) : _rb(rb) {}

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
    osg::ref_ptr<PickReadback> _rb;
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

    int pickSize = 1;
    args.read("--pick-size", pickSize);
    if(pickSize < 1) pickSize = 1;

    OSG_NOTICE
        << "Pick mode: image readback (osg::Image, 1-frame lag)"
        << "  region: " << pickSize << "x" << pickSize
        << " (rule: pickCenter)"
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

    auto pickImage = osgx::make_ref<osg::Image>();
    pickImage->allocateImage(W, H, 1, GL_RGBA, GL_UNSIGNED_BYTE);

    auto scene   = createScene();
    auto pickCam = createPickCamera(W, H, pickImage.get());

    // Pick camera renders the same scene node (shared ref_ptr)
    pickCam->addChild(scene);

    // Sync pick camera's view/projection to the viewer's main camera every update traversal.
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

    // Install the readback as an update callback on the pick camera so it runs each
    // update traversal and reads from the image OSG populated during the previous render.
    auto rb = osgx::make_ref<PickReadback>(pickSize, pickCenter, pickImage.get());
    pickCam->setUpdateCallback(rb);

    viewer.addEventHandler(new PickHandler(rb));
    viewer.setSceneData(root);

    return viewer.run();
}
