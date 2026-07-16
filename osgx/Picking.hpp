#pragma once

#include "Core.hpp"

namespace osgx {

// ================================================================================================
// Texture-based object-ID picking; the caller is responsible for:
//
// 1. Creating a pick camera (RTT FBO, ABSOLUTE_RF, pick shader with OVERRIDE).
// 2. Sharing the scene node as a child of both the pick camera and the main scene root.
// 3. Syncing the pick camera's view/projection to the main camera each update traversal (PickCameraSync).
// 4. Adding PickHandler as a viewer event handler.
//
// See examples/osgdebug-picking.cpp for a complete worked example including all three FBO
// modes (full WxH, --small-pick N, --pick-1x1 sub-frustum).
//
// NOTE: every RTT camera that owns its own view/projection matrices MUST call
// `cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF)`. Without it the cull frustum is composed
// with the parent transform stack, all geometry is silently clipped, and the FBO renders only the
// clear color.
// ================================================================================================

// ------------------------------------------------------------------------------------------------
// Pick shader strings -- hook-based design, multi-shader-object linking
//
// Two shader objects per stage: core declares the hook prototype, hook provides the definition.
// Swap only the hook to change picking behavior without recompiling the core.
//
// pickVertexHook() -- end of vertex stage; forward per-vertex attributes to the frag stage.
// Default: no-op (PICK_VERT_HOOK_NOOP).
//
// getPickID() -- fragment stage; return the pick ID for this fragment.
// Default: reads uniform uint pickID (PICK_FRAG_HOOK_UNIFORM).
//
// makePickCamera() assembles these into a program and installs it with OVERRIDE.
// ------------------------------------------------------------------------------------------------

// Core: declares the hook prototypes and provides the main() implementations.
inline constexpr const char* PICK_VERT_CORE = R"GLSL(
#version 330 core
in vec4 osg_Vertex;
uniform mat4 osg_ModelViewProjectionMatrix;
void pickVertexHook();
void main() {
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
	pickVertexHook();
}
)GLSL";

inline constexpr const char* PICK_FRAG_CORE = R"GLSL(
#version 330 core
out vec4 fragColor;
uint getPickID();
void main() {
	uint id = getPickID();
	fragColor = vec4(
		float( id & 0xFFu) / 255.0,
		float((id >> 8u) & 0xFFu) / 255.0,
		float((id >> 16u) & 0xFFu) / 255.0,
		float((id >> 24u) & 0xFFu) / 255.0
	);
}
)GLSL";

// Default hooks: no vertex forwarding; fragment reads uniform uint pickID.
inline constexpr const char* PICK_VERT_HOOK_NOOP = "void pickVertexHook() {}";
inline constexpr const char* PICK_FRAG_HOOK_UNIFORM = "uniform uint pickID; uint getPickID() { return pickID; }";

// Creates a POST_RENDER FBO camera wired for object-ID picking.
//
// ABSOLUTE_RF is set so the camera uses its own view/projection matrices independently
// of where it sits in the scene graph -- without it the cull frustum is wrong and all
// geometry is silently clipped.
//
// image=nullptr (ASYNC mode): attach a renderbuffer instead of an osg::Image so the
// camera renders into an FBO without triggering OSG's internal glReadPixels. The caller
// installs a postDrawCallback (e.g. AsyncReadback) to issue its own PBO-based readback.
//
// The caller is responsible for:
//
// - addChild(scene) on the returned camera
// - syncing view/projection from the main camera each update traversal
// - installing a readback callback (update NodeCallback for SYNC; postDrawCallback for ASYNC)
inline osg::ref_ptr<osg::Camera> makePickCamera(
	int w, int h,
	osg::Image* image = nullptr,
	osg::Shader* vertHook = nullptr,
	osg::Shader* fragHook = nullptr
) {
	auto cam = make_ref<osg::Camera>();

	cam->setName("PickCamera");
	cam->setRenderOrder(osg::Camera::POST_RENDER);
	cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	cam->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	cam->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f)); // all-zero RGBA = ID 0 = no pick
	cam->setViewport(0, 0, w, h);
	// Without ABSOLUTE_RF the camera composes view/projection with the parent
	// transform stack, producing a wrong cull frustum that clips all geometry.
	cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	cam->setSmallFeatureCullingPixelSize(-1.0f);

	if(image) cam->attach(osg::Camera::COLOR_BUFFER, image);
	else cam->attach(osg::Camera::COLOR_BUFFER, GL_RGBA);

	auto prog = make_ref<osg::Program>();

	prog->setName("pickProgram");

	auto* vc = new osg::Shader(osg::Shader::VERTEX, PICK_VERT_CORE);
	auto* vh = vertHook ? vertHook : new osg::Shader(osg::Shader::VERTEX, PICK_VERT_HOOK_NOOP);
	auto* fc = new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_CORE);
	auto* fh = fragHook ? fragHook : new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_HOOK_UNIFORM);

	vc->setName("pickVertCore"); vh->setName("pickVertHook");
	fc->setName("pickFragCore"); fh->setName("pickFragHook");

	prog->addShader(vc); prog->addShader(vh);
	prog->addShader(fc); prog->addShader(fh);

	auto* ss = cam->getOrCreateStateSet();

	// BlendFunc(ONE,ZERO) with PROTECTED: osg::Text re-enables blend without
	// respecting OVERRIDE alone; PROTECTED prevents any child from overriding it.
	auto* bf = new osg::BlendFunc(
		osg::BlendFunc::ONE, osg::BlendFunc::ZERO,
		osg::BlendFunc::ONE, osg::BlendFunc::ZERO
	);

	ss->setMode(GL_DITHER, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
	ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setAttributeAndModes(
		bf,
		osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED
	);

	return cam;
}

// ASYNC variant: attach a Texture2D for FBO rendering. OSG uses glFramebufferTexture2D
// (renders directly into the texture) with no automatic CPU readback. The caller installs
// a postDrawCallback that uses glGetTexImage + PBO for zero-stall async readback -- valid
// after FBO unbind since glGetTexImage reads from the texture object, not the framebuffer.
// makePickCamera configures the texture's size, format, and NEAREST filters.
inline osg::ref_ptr<osg::Camera> makePickCamera(
	int w, int h,
	osg::Texture2D* tex,
	osg::Shader* vertHook = nullptr,
	osg::Shader* fragHook = nullptr
) {
	tex->setTextureSize(w, h);
	tex->setInternalFormat(GL_RGBA);
	tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);

	auto cam = make_ref<osg::Camera>();

	cam->setName("PickCamera");
	cam->setRenderOrder(osg::Camera::POST_RENDER);
	cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	cam->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	cam->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f)); // all-zero RGBA = ID 0 = no pick
	cam->setViewport(0, 0, w, h);
	cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	cam->setSmallFeatureCullingPixelSize(-1.0f);
	cam->attach(osg::Camera::COLOR_BUFFER, tex);

	auto prog = make_ref<osg::Program>();

	prog->setName("pickProgram");

	auto* vc = new osg::Shader(osg::Shader::VERTEX, PICK_VERT_CORE);
	auto* vh = vertHook ? vertHook : new osg::Shader(osg::Shader::VERTEX, PICK_VERT_HOOK_NOOP);
	auto* fc = new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_CORE);
	auto* fh = fragHook ? fragHook : new osg::Shader(osg::Shader::FRAGMENT, PICK_FRAG_HOOK_UNIFORM);

	vc->setName("pickVertCore"); vh->setName("pickVertHook");
	fc->setName("pickFragCore"); fh->setName("pickFragHook");

	prog->addShader(vc); prog->addShader(vh);
	prog->addShader(fc); prog->addShader(fh);

	auto* ss = cam->getOrCreateStateSet();

	auto* bf = new osg::BlendFunc(
		osg::BlendFunc::ONE, osg::BlendFunc::ZERO,
		osg::BlendFunc::ONE, osg::BlendFunc::ZERO
	);

	ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setAttributeAndModes(bf, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED);
	ss->setMode(GL_DITHER, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

	return cam;
}

// Decode a 32-bit pick ID from an RGBA pixel: R=bits[7:0], G=bits[15:8], B=bits[23:16], A=bits[31:24].
inline uint32_t decodePickID(const uint8_t* px) {
	return uint32_t(px[0]) | (uint32_t(px[1]) << 8) | (uint32_t(px[2]) << 16) | (uint32_t(px[3]) << 24);
}

// Selects one pick ID from a flat NxN RGBA pixel buffer.
// pixels -- row-major RGBA, n*n pixels, Y=0 at bottom-left (OpenGL convention)
// n -- side length of the region; n=1 is the degenerate single-pixel case
using PickRule = std::function<uint32_t(const uint8_t*, int)>;

// Center pixel wins -- semantics identical to n=1; larger n widens the rasterized region.
inline uint32_t pickCenter(const uint8_t* px, int n) {
	int half = n / 2;

	return decodePickID(px + (half * n + half) * 4);
}

// Most-covered non-zero ID wins -- useful in dense or overlapping scenes.
inline uint32_t pickMostCoverage(const uint8_t* px, int n) {
	std::unordered_map<uint32_t, unsigned int> counts;

	for(int i = 0; i < n * n; i++) {
		uint32_t id = decodePickID(px + i * 4);

		if(id) counts[id]++;
	}

	if(counts.empty()) return 0;

	return std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
		return a.second < b.second;
	})->first;
}

// Non-zero ID nearest to center wins -- good for snap-to-object / hover picking.
inline uint32_t pickNearestToCenter(const uint8_t* px, int n) {
	int half = n / 2;
	int bestD = n * n + 1;
	uint32_t bestID = 0;

	for(int row = 0; row < n; row++) {
		for(int col = 0; col < n; col++) {
			uint32_t id = decodePickID(px + (row * n + col) * 4);

			if(!id) continue;

			int dx = col - half, dy = row - half;
			int d = dx * dx + dy * dy;

			if(d < bestD) { bestD = d; bestID = id; }
		}
	}

	return bestID;
}

// Spirals outward from center ring by ring (Chebyshev distance), returning the first
// non-zero ID found. Equivalent result to pickNearestToCenter but exits on the first
// hit instead of scanning all N*N pixels -- preferred default for all pick modes.
inline uint32_t spiralPick(const uint8_t* px, int n) {
	int half = n / 2;
	uint32_t id = decodePickID(px + (half * n + half) * 4);

	if(id) return id;

	for(int r = 1; r <= half; r++) {
		// Top edge: row = half+r, col = half-r .. half+r
		for(int c = -r; c <= r; c++) {
			id = decodePickID(px + ((half + r) * n + (half + c)) * 4);

			if(id) return id;
		}

		// Right edge: col = half+r, row = half+r-1 .. half-r
		for(int rr = r - 1; rr >= -r; rr--) {
			id = decodePickID(px + ((half + rr) * n + (half + r)) * 4);

			if(id) return id;
		}

		// Bottom edge: row = half-r, col = half+r-1 .. half-r
		for(int c = r - 1; c >= -r; c--) {
			id = decodePickID(px + ((half - r) * n + (half + c)) * 4);

			if(id) return id;
		}

		// Left edge: col = half-r, row = half-r+1 .. half+r-1
		for(int rr = -r + 1; rr <= r - 1; rr++) {
			id = decodePickID(px + ((half + rr) * n + (half - r)) * 4);

			if(id) return id;
		}
	}

	return 0;
}

enum class ActionType { HOVER, CLICK };

// Shared state for SYNC and ASYNC pick readback variants.
// Plain struct - not osg::Referenced. Concrete classes inherit from this alongside
// a callback base (NodeCallback or Camera::DrawCallback) that provides ref-counting.
//
// onPick(id, ActionType) -- HOVER fires when hovered ID changes (including to 0=background);
//                           CLICK fires when a click resolves.
//                           May fire on any thread depending on readback mode -- safe for
//                           logging/audio/non-scene-graph reactions only.
// onEnter(id)            -- non-zero id entered. Always fired from the update thread via
// onLeave(id)               PickHoverCallback -- safe for scene graph modifications.
// reportClick()          -- call from PickHandler in CONTINUOUS mode to fire CLICK with the
//                           currently hovered ID.
struct PickReadback {
	std::function<void(uint32_t, ActionType)> onPick;
	std::function<void(uint32_t)> onEnter;
	std::function<void(uint32_t)> onLeave;

	void requestPick(int x, int y) {
		_x.store(x, std::memory_order_relaxed);
		_y.store(y, std::memory_order_relaxed);
		_requested.store(true, std::memory_order_release);
	}

	void updateMouse(int x, int y) {
		_x.store(x, std::memory_order_relaxed);
		_y.store(y, std::memory_order_relaxed);
	}

	void reportClick() {
		if(onPick) onPick(_lastID.load(std::memory_order_acquire), ActionType::CLICK);
	}

	int mouseX() const { return _x.load(std::memory_order_relaxed); }
	int mouseY() const { return _y.load(std::memory_order_relaxed); }
	uint32_t lastID() const { return _lastID.load(std::memory_order_acquire); }

protected:
	void _fireHover(uint32_t id) const {
		_lastID.store(id, std::memory_order_release);

		if(id == _prevID) return;
		_prevID = id;

		if(onPick) onPick(id, ActionType::HOVER);
	}

	void _fireClick(uint32_t id) const {
		_lastID.store(id, std::memory_order_release);

		if(onPick) onPick(id, ActionType::CLICK);
	}

	mutable std::atomic<int> _x{0}, _y{0};
	mutable std::atomic<bool> _requested{false};
	mutable std::atomic<uint32_t> _lastID{0};
	mutable uint32_t _prevID{0};
};

// SYNC readback: NodeCallback that reads from an osg::Image attached to the pick camera.
//
// OSG reads the FBO into the image inside RenderStage::drawImplementation while the FBO is
// still bound. We sample image->data() one frame later in the update traversal -- invisible
// latency for click-only or continuous hover picking.
class PickReadbackSync: public PickReadback, public osg::NodeCallback {
public:
	enum class Mode { CLICK, CONTINUOUS };

	// winW / winH -- actual window dimensions; used to scale mouse coords when the pick
	// image is smaller than the window (small-pick mode).
	PickReadbackSync(
		int pickSize,
		PickRule rule,
		osg::Image* image,
		int winW,
		int winH,
		Mode mode = Mode::CLICK
	):
	_pickSize(pickSize),
	_rule(std::move(rule)),
	_winW(winW),
	_winH(winH),
	_mode(mode),
	_image(image) {}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		bool doRead =
			(_mode == Mode::CONTINUOUS) ||
			_requested.exchange(false, std::memory_order_acq_rel)
		;

		if(doRead) {
			int imgW = _image->s();
			int imgH = _image->t();
			const uint8_t* data = _image->data();

			if(data) {
				int imgX, imgY;

				if(_mode == Mode::CONTINUOUS) {
					imgX = imgY = 0; // 1x1 FBO: the single pixel is always at [0,0]
				}

				else {
					int wx = _x.load(std::memory_order_relaxed);
					int wy = _y.load(std::memory_order_relaxed);

					imgX = (imgW == _winW) ? wx : wx * imgW / _winW;
					imgY = (imgH == _winH) ? wy : wy * imgH / _winH;
					imgX = std::clamp(imgX, 0, imgW - 1);
					imgY = std::clamp(imgY, 0, imgH - 1);
				}

				int N = _pickSize;
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

				if(_mode == Mode::CONTINUOUS) _fireHover(id);
				else _fireClick(id);
			}
		}

		traverse(node, nv);
	}

private:
	int _pickSize;
	PickRule _rule;
	int _winW, _winH;
	Mode _mode;
	osg::ref_ptr<osg::Image> _image;
};

// ASYNC readback: Camera::DrawCallback using a Texture2D attachment + PBO glGetTexImage.
// Install as postDrawCallback on the pick camera.
//
// The pick camera must attach a Texture2D (not osg::Image) so OSG renders directly into
// the texture with no automatic CPU readback. glGetTexImage reads from the texture object --
// FBO binding is irrelevant after unbind -- so the postDrawCallback timing is correct.
// TODO: This is only PBO-delayed readback, not strictly non-stalling async: the next-frame
// glMapBuffer() can still block if the transfer has not completed. Fix by adding a fence
// (glFenceSync + zero-timeout glClientWaitSync polling) or a deeper PBO ring before mapping.
//
// CLICK mode: one async download per requestPick() call.
// CONTINUOUS mode: download every frame (negligible for a 1x1 texture).
class PickReadbackAsync: public PickReadback, public osg::Camera::DrawCallback {
public:
	enum class Mode { CLICK, CONTINUOUS };

	PickReadbackAsync(osg::Texture2D* tex, int imgW, int imgH, Mode mode = Mode::CLICK):
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

		if(_inFlight) {
			ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
			auto* ptr = static_cast<const uint8_t*>(
				ext->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)
			);
			if(ptr) {
				int px = std::clamp(_pickX, 0, _imgW - 1);
				int py = std::clamp(_pickY, 0, _imgH - 1);
				uint32_t id = decodePickID(ptr + (py * _imgW + px) * 4);
				if(_mode == Mode::CLICK) _fireClick(id);
				else _fireHover(id);
				ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			ext->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			_inFlight = false;
		}

		bool doDownload =
			(_mode == Mode::CONTINUOUS) ||
			_requested.exchange(false, std::memory_order_acq_rel);

		if(doDownload) {
			_pickX = _x.load(std::memory_order_relaxed);
			_pickY = _y.load(std::memory_order_relaxed);
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

	mutable GLuint _pbo{0};
	mutable bool _init{false};
	mutable bool _inFlight{false};
	mutable int _pickX{0}, _pickY{0};
};

// NodeCallback that syncs a pick camera's view/projection from the viewer camera each
// update traversal. Install directly on the pick camera via setUpdateCallback(); chain
// other pick callbacks (e.g. PickReadbackSync) via setNestedCallback().
//
// pick1x1=true: also builds a sub-frustum projection centered on the cursor each frame
// (gluPickMatrix equivalent). Requires rb for the current mouse position.
class PickCameraSync: public osg::NodeCallback {
public:
	PickCameraSync(
		osg::Camera* viewerCam,
		bool pick1x1 = false,
		int W = 0,
		int H = 0,
		PickReadback* rb = nullptr
	):
	_viewerCam(viewerCam),
	_pick1x1(pick1x1),
	_W(W),
	_H(H),
	_rb(rb) {}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		if(auto* vc = _viewerCam.get()) {
			auto* cam = static_cast<osg::Camera*>(node);

			cam->setViewMatrix(vc->getViewMatrix());

			if(_pick1x1 && _rb) {
				double cx = _rb->mouseX() + 0.5;
				double cy = _rb->mouseY() + 0.5;
				double W  = static_cast<double>(_W);
				double H  = static_cast<double>(_H);

				osg::Matrix sub(W, 0, 0, 0, 0, H, 0, 0, 0, 0, 1, 0, W - 2.0*cx, H - 2.0*cy, 0, 1);

				cam->setProjectionMatrix(vc->getProjectionMatrix() * sub);
			} else {
				cam->setProjectionMatrix(vc->getProjectionMatrix());
			}
		}

		traverse(node, nv);
	}

private:
	osg::observer_ptr<osg::Camera> _viewerCam;
	bool _pick1x1;
	int _W, _H;
	PickReadback* _rb;
};

// NodeCallback that fires onEnter/onLeave on the update thread by polling lastID().
//
// Install on any node in the update traversal alongside or instead of PickReadbackSync.
// The readback (SYNC or ASYNC) updates _lastID atomically; this callback detects transitions
// and fires callbacks safely regardless of which thread the readback runs on.
//
// This is the correct way to trigger scene graph modifications (setMatrix, setColor, etc.)
// in response to hover events -- onEnter/onLeave fired directly from PickReadbackAsync's
// draw callback would race with the cull thread.
class PickHoverCallback: public osg::NodeCallback {
public:
	explicit PickHoverCallback(PickReadback* rb): _rb(rb) {}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		uint32_t id = _rb->lastID();

		if(id != _prevID) {
			uint32_t prev = _prevID;
			_prevID = id;

			if(prev != 0 && _rb->onLeave) _rb->onLeave(prev);
			if(id   != 0 && _rb->onEnter) _rb->onEnter(id);
		}

		traverse(node, nv);
	}

private:
	PickReadback* _rb;
	uint32_t      _prevID{0};
};

// GUIEventHandler that forwards click/move events to any PickReadback variant.
// continuous=false -- left-click calls requestPick(x, y); use with CLICK mode.
// continuous=true -- MOVE events update cursor position; left-click queries lastID().
// Use with CONTINUOUS mode (1x1 sub-frustum picking).
//
// consumeEvents=true -- returns true on left-click, stopping OSG's handler chain.
// Use when picking should be exclusive (e.g. object selection that must not also
// rotate the camera). Default false (additive -- pick and camera manipulator both
// receive the click).
class PickHandler: public osgGA::GUIEventHandler {
public:
	// rb is non-owning; kept alive by the camera's callback ref for its lifetime.
	explicit PickHandler(PickReadback* rb, bool continuous=false, bool consumeEvents=false):
	_rb(rb),
	_continuous(continuous),
	_consume(consumeEvents) {}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
		int x = static_cast<int>(ea.getX());
		int y = static_cast<int>(ea.getY());

		if(ea.getEventType() == osgGA::GUIEventAdapter::MOVE && _continuous) {
			_rb->updateMouse(x, y);

			return false;
		}

		if(
			ea.getEventType() == osgGA::GUIEventAdapter::PUSH &&
			ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON
		) {
			if(_continuous) _rb->reportClick();
			else _rb->requestPick(x, y);

			if(_consume) return true;
		}

		return false;
	}

private:
	PickReadback* _rb;

	bool _continuous;
	bool _consume;
};


}
