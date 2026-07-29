#include "osgx/Picking.hpp"

namespace osgx {

namespace {

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
constexpr const char* PICK_VERT_CORE = R"GLSL(
#version 330 core
in vec4 osg_Vertex;
uniform mat4 osg_ModelViewProjectionMatrix;
void pickVertexHook();
void main() {
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
	pickVertexHook();
}
)GLSL";

constexpr const char* PICK_FRAG_CORE = R"GLSL(
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
constexpr const char* PICK_VERT_HOOK_NOOP = "void pickVertexHook() {}";
constexpr const char* PICK_FRAG_HOOK_UNIFORM = "uniform uint pickID; uint getPickID() { return pickID; }";

}

osg::ref_ptr<osg::Camera> makePickCamera(
	int w, int h,
	osg::Image* image,
	osg::Shader* vertHook,
	osg::Shader* fragHook
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

osg::ref_ptr<osg::Camera> makePickCamera(
	int w, int h,
	osg::Texture2D* tex,
	osg::Shader* vertHook,
	osg::Shader* fragHook
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

uint32_t pickCenter(const uint8_t* px, int n) {
	int half = n / 2;

	return decodePickID(px + (half * n + half) * 4);
}

uint32_t pickMostCoverage(const uint8_t* px, int n) {
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

uint32_t pickNearestToCenter(const uint8_t* px, int n) {
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

uint32_t spiralPick(const uint8_t* px, int n) {
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

void PickReadback::requestPick(int x, int y) {
	_x.store(x, std::memory_order_relaxed);
	_y.store(y, std::memory_order_relaxed);
	_requested.store(true, std::memory_order_release);
}

void PickReadback::updateMouse(int x, int y) {
	_x.store(x, std::memory_order_relaxed);
	_y.store(y, std::memory_order_relaxed);
}

void PickReadback::reportClick() {
	if(onPick) onPick(_lastID.load(std::memory_order_acquire), ActionType::CLICK);
}

void PickReadback::_fireHover(uint32_t id) const {
	_lastID.store(id, std::memory_order_release);

	if(id == _prevID) return;
	_prevID = id;

	if(onPick) onPick(id, ActionType::HOVER);
}

void PickReadback::_fireClick(uint32_t id) const {
	_lastID.store(id, std::memory_order_release);

	if(onPick) onPick(id, ActionType::CLICK);
}

void PickReadbackSync::operator()(osg::Node* node, osg::NodeVisitor* nv) {
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

void PickReadbackAsync::operator()(osg::RenderInfo& ri) const {
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

void PickCameraSync::operator()(osg::Node* node, osg::NodeVisitor* nv) {
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

void PickHoverCallback::operator()(osg::Node* node, osg::NodeVisitor* nv) {
	uint32_t id = _rb->lastID();

	if(id != _prevID) {
		uint32_t prev = _prevID;
		_prevID = id;

		if(prev != 0 && _rb->onLeave) _rb->onLeave(prev);
		if(id   != 0 && _rb->onEnter) _rb->onEnter(id);
	}

	traverse(node, nv);
}

bool PickHandler::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) {
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

}
