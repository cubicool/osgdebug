#pragma once

#include "osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GLExtensions>
#include <osg/Drawable>
#include <osg/Camera>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <unordered_map>

// TODO: Add notification mirroring of debug calls (to std::cout, osg::notify, etc).
// TODO: Make some macro wrappers for pushGroup/insertMessage that use __FUNCTION__, __FILE__, etc.
// TODO: pushGroup/insertMessage accept an "id", which we probably should manage automatically.

namespace osgDebug {

using namespace osgx::literals;

inline constexpr size_t DEFAULT_BUFFER_SIZE = 60;

enum class Severity: GLenum {
	DONT_CARE = 0x1100,
	HIGH = 0x9146,
	LOW = 0x9148,
	MEDIUM = 0x9147,
	NOTIFICATION = 0x826B
};

// TODO: The default "source" (for pushGroup/messageInsert) is APPLICATION; does this make sense?
// What are the others even FOR!?
enum class Source: GLenum {
	DONT_CARE = 0x1100,
	API = 0x8246,
	APPLICATION = 0x824A,
	OTHER = 0x824B,
	SHADER_COMPILER = 0x8248,
	THIRD_PARTY = 0x8249,
	WINDOW_SYSTEM = 0x8247
};

enum class Type: GLenum {
	DONT_CARE = 0x1100,
	DEPRECATED_BEHAVIOR = 0x824D,
	ERROR = 0x824C,
	MARKER = 0x8268,
	OTHER = 0x8251,
	PERFORMANCE = 0x8250,
	POP_GROUP = 0x826A,
	PORTABILITY = 0x824F,
	PUSH_GROUP = 0x8269,
	UNDEFINED_BEHAVIOR = 0x824E
};

namespace detail {
	inline const char* toString(Source s) {
		switch(s) {
			case Source::DONT_CARE: return "DONT_CARE";
			case Source::API: return "API";
			case Source::APPLICATION: return "APPLICATION";
			case Source::OTHER: return "OTHER";
			case Source::SHADER_COMPILER: return "SHADER_COMPILER";
			case Source::THIRD_PARTY: return "THIRD_PARTY";
			case Source::WINDOW_SYSTEM: return "WINDOW_SYSTEM";
		}

		return "UNKNOWN_SOURCE";
	}

	inline const char* toString(Type t) {
		switch(t) {
			case Type::DONT_CARE: return "DONT_CARE";
			case Type::DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
			case Type::ERROR: return "ERROR";
			case Type::MARKER: return "MARKER";
			case Type::OTHER: return "OTHER";
			case Type::PERFORMANCE: return "PERFORMANCE";
			case Type::POP_GROUP: return "POP_GROUP";
			case Type::PORTABILITY: return "PORTABILITY";
			case Type::PUSH_GROUP: return "PUSH_GROUP";
			case Type::UNDEFINED_BEHAVIOR: return "UNDEFINED_BEHAVIOR";
		}

		return "UNKNOWN_TYPE";
	}

	inline const char* toString(Severity s) {
		switch(s) {
			case Severity::DONT_CARE: return "DONT_CARE";
			case Severity::HIGH: return "HIGH";
			case Severity::LOW: return "LOW";
			case Severity::MEDIUM: return "MEDIUM";
			case Severity::NOTIFICATION: return "NOTIFICATION";
		}

		return "UNKNOWN_SEVERITY";
	}
}

namespace detail {
	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPushDebugGroup.xhtml
	using glPushDebugGroupFunc = void(APIENTRYP)(GLenum, GLuint, GLsizei, const char*);

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPopDebugGroup.xhtml
	using glPopDebugGroupFunc = void(APIENTRYP)();

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageInsert.xhtml
	using glDebugMessageInsertFunc = void(APIENTRYP)(GLenum, GLenum, GLuint, GLenum, GLsizei, const char*);

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageCallback.xhtml
	using glDebugMessageCallbackFunc = void(APIENTRYP)(GLDEBUGPROC, const void*);

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageControl.xhtml
	using glDebugMessageControlFunc = void(APIENTRYP)(
		GLenum source,
		GLenum type,
		GLenum severity,
		GLsizei count,
		const GLuint* ids,
		GLboolean enabled
	);

	inline glPushDebugGroupFunc _pushGroup = nullptr;
	inline glPopDebugGroupFunc _popGroup = nullptr;
	inline glDebugMessageInsertFunc _messageInsert = nullptr;
	inline glDebugMessageCallbackFunc _messageCallback = nullptr;
	inline glDebugMessageControlFunc _messageControl = nullptr;
	inline osg::NotifySeverity _defaultCallbackNotifySeverity = osg::NOTICE;

	inline constexpr void print(const auto&... args) {
		// TODO: I don' think this is correct... it should probably have it's "own thing."
		((osg::notify(_defaultCallbackNotifySeverity) << args), ...) << std::endl;
	}

	inline void APIENTRY defaultCallback(
		GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		const GLchar* message,
		const void* userParam
	) {
		// (void)userParam;

		const auto src = static_cast<Source>(source);
		const auto typ = static_cast<Type>(type);
		const auto sev = static_cast<Severity>(severity);

		std::string msg;

		if(message) {
			if(length >= 0) msg.assign(message, static_cast<size_t>(length));
			else msg = message;
		}

		osg::notify(_defaultCallbackNotifySeverity)
			<< "osgDebug | source=" << toString(src)
			<< " type=" << toString(typ)
			<< " severity=" << toString(sev)
			<< " id=" << id
			<< " | " << msg
			<< std::endl
		;
	}

	inline void pushGroup(Source source, GLuint id, const std::string& message) {
		if(!_pushGroup) return;

		_pushGroup(
			static_cast<std::underlying_type_t<Source>>(source),
			id,
			-1,
			message.c_str()
		);

		// TODO: Temporary! But lets us append (almost) anything as just an extra arg! :)
		print("osgDebug::pushGroup | ", message);
	}

	inline void popGroup() {
		if(!_popGroup) return;

		_popGroup();
	}

	inline void messageInsert(
		GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		const std::string& message
	) {
		if(!_messageInsert) return;

		_messageInsert(source, type, id, severity, -1, message.c_str());

		// TODO: Temporary!
		print("osgDebug::messageInsert | ", message);
	}

	inline void setCallback(GLDEBUGPROC callback, const void* userParam=nullptr) {
		if(!_messageCallback) return;

		_messageCallback(callback, userParam);
	}

	inline void clearCallback() {
		setCallback(nullptr, nullptr);
	}

	inline void enableDebugOutput(bool synchronous=true) {
		if(!_messageCallback) return;

		glEnable(GL_DEBUG_OUTPUT);

		if(synchronous) glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		else glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

	inline void disableDebugOutput() {
		if(!_messageCallback) return;

		glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDisable(GL_DEBUG_OUTPUT);
	}

	inline void installDefaultCallback(
		bool synchronous=true,
		osg::NotifySeverity notifySeverity=osg::NOTICE
	) {
		if(!_messageCallback) return;

		_defaultCallbackNotifySeverity = notifySeverity;

		enableDebugOutput(synchronous);
		setCallback(defaultCallback, nullptr);
	}

	inline void messageControl(
		GLenum source,
		GLenum type,
		GLenum severity,
		bool enabled,
		GLsizei count=0,
		const GLuint* ids=nullptr
	) {
		if(!_messageControl) return;

		_messageControl(
			source,
			type,
			severity,
			count,
			ids,
			enabled ? GL_TRUE : GL_FALSE
		);
	}

	template<typename T>
	inline void setupFunction(const std::string& name, T* func) {
		void* f = osg::getGLExtensionFuncPtr(name.c_str());

		if(f) {
			*func = reinterpret_cast<T>(f);

			print(" >> Bound function '", name, "' to @", (void*)(*func));
		}

		else print(" >> FAILED to bind '", name, "'");
	}

	// Per-context accumulator for two-phase GPU timing. DrawCallback (phase 1) pushes
	// pending timestamp query pairs here without blocking. FinalDrawCallback (phase 2)
	// syncs once and drains all results after the full camera has rendered.
	//
	// Keyed by contextID via osg::buffered_object so each GL context has its own
	// isolated accumulator; GL query objects are context-local.
	struct FrameAccumulator {
		struct Entry {
			std::string path;

			GLuint begin = 0;
			GLuint end = 0;
		};

		struct PathStats {
			osgx::aring_buffer<GLuint64, DEFAULT_BUFFER_SIZE> gpuBuffer;

			size_t samplesSincePrint = 0;
		};

		std::vector<Entry> pending;
		std::vector<Entry> ready;
		std::vector<Entry> freeList;
		std::unordered_map<std::string, PathStats> stats;

		Entry alloc(osg::GLExtensions* ext) {
			if(!freeList.empty()) {
				auto e = std::move(freeList.back());

				freeList.pop_back();

				return e;
			}

			Entry e;

			ext->glGenQueries(1, &e.begin);
			ext->glGenQueries(1, &e.end);

			return e;
		}

		void push(Entry e) {
			pending.push_back(std::move(e));
		}

		// SYNC: drain current frame's pending queries, blocking per-query.
		void drain(osg::GLExtensions* ext, size_t printEvery, unsigned int frameNum) {
			_drain(pending, ext, printEvery, frameNum);
		}

		// ASYNC: drain the previous frame's results (should already be ready),
		// then advance the double-buffer so this frame's pending becomes next
		// frame's ready. One-frame lag; ring-buffer averaging makes it irrelevant.
		void swap_and_drain(osg::GLExtensions* ext, size_t printEvery, unsigned int frameNum) {
			_drain(ready, ext, printEvery, frameNum);

			std::swap(pending, ready);
		}

	private:
		void _drain(std::vector<Entry>& source, osg::GLExtensions* ext, size_t printEvery, unsigned int frameNum) {
			for(auto& e : source) {
				GLuint64 t0 = 0, t1 = 0;

				ext->glGetQueryObjectui64v(e.begin, GL_QUERY_RESULT, &t0);
				ext->glGetQueryObjectui64v(e.end, GL_QUERY_RESULT, &t1);

				const GLuint64 gpuNs = t1 - t0;
				auto& s = stats[e.path];

				s.gpuBuffer.add(gpuNs);
				s.samplesSincePrint++;

				if(s.samplesSincePrint >= printEvery) {
					s.samplesSincePrint = 0;

					// TODO: This is annoyingly AWFUL and should be fixed; SOON!
					print(
						" >> [", e.path, "] GPU: ", gpuNs / 1000u, "us",
						" | avg: ", s.gpuBuffer.average(printEvery) / 1000u, "us",
						" Frame: ", frameNum
					);
				}

				freeList.push_back(std::move(e));
			}

			source.clear();
		}
	};

	inline osg::buffered_object<FrameAccumulator> _accumulators;
}

inline void pushGroup(Source source, GLuint id, const std::string& message) {
	detail::pushGroup(source, id, message);
}

inline void pushGroup(GLuint id, const std::string& message) {
	pushGroup(Source::APPLICATION, id, message);
}

inline void popGroup() {
	detail::popGroup();
}

inline void messageInsert(
	Source source,
	Type type,
	GLuint id,
	Severity severity,
	const std::string& message
) {
	detail::messageInsert(
		static_cast<std::underlying_type_t<Source>>(source),
		static_cast<std::underlying_type_t<Type>>(type),
		id,
		static_cast<std::underlying_type_t<Severity>>(severity),
		message
	);
}

inline void messageInsert(
	Type type,
	GLuint id,
	Severity severity,
	const std::string& message
) {
	messageInsert(Source::APPLICATION, type, id, severity, message);
}

// TODO: More messageInsert() wrappers for Type/Severity.

inline void setCallback(GLDEBUGPROC callback, const void* userParam=nullptr) {
	detail::setCallback(callback, userParam);
}

inline void clearCallback() {
	detail::clearCallback();
}

inline void installDefaultCallback(
	bool synchronous=true,
	osg::NotifySeverity notifySeverity=osg::NOTICE
) {
	detail::installDefaultCallback(synchronous, notifySeverity);
}

inline void enableDebugOutput(bool synchronous=true) {
	detail::enableDebugOutput(synchronous);
}

inline void disableDebugOutput() {
	detail::disableDebugOutput();
}

inline void messageControl(
	Source source,
	Type type,
	Severity severity,
	bool enabled,
	GLsizei count=0,
	const GLuint* ids=nullptr
) {
	detail::messageControl(
		static_cast<std::underlying_type_t<Source>>(source),
		static_cast<std::underlying_type_t<Type>>(type),
		static_cast<std::underlying_type_t<Severity>>(severity),
		enabled,
		count,
		ids
	);
}

// TODO: Add a check for whether we're already initialized.
inline void initialize(osg::GraphicsContext* gc) {
	if(osg::isGLExtensionSupported(gc->getState()->getContextID(), "GL_KHR_debug")) {
		detail::setupFunction("glPushDebugGroup", &detail::_pushGroup);
		detail::setupFunction("glPopDebugGroup", &detail::_popGroup);
		detail::setupFunction("glDebugMessageInsert", &detail::_messageInsert);
		detail::setupFunction("glDebugMessageCallback", &detail::_messageCallback);
		detail::setupFunction("glDebugMessageControl", &detail::_messageControl);
	}
}

inline void deinitialize(bool disableOutput=true) {
	clearCallback();

	if(disableOutput && detail::_messageCallback) disableDebugOutput();

	detail::_pushGroup = nullptr;
	detail::_popGroup = nullptr;
	detail::_messageInsert = nullptr;
	detail::_messageCallback = nullptr;
	detail::_messageControl = nullptr;
	detail::_defaultCallbackNotifySeverity = osg::NOTICE;
}

class DebugGroupAnnotation: public osg::Referenced {
public:
	DebugGroupAnnotation(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_id(id),
	_source(source),
	_message(message),
	_measureTime(measureTime) {}

	void begin() {
		_active = detail::_pushGroup != nullptr;

		if(_active) pushGroup(_source, _id, _message);

		if(_measureTime) _start = osg::Timer::instance()->tick();
	}

	void end() {
		if(!_active) return;

		if(_measureTime) {
			auto stop = osg::Timer::instance()->tick();
			auto dt = stop - _start;
			// TODO: This is more correct?
			// auto dt = osg::Timer::instance()->delta_u(_start, stop);

			messageInsert(
				Type::PERFORMANCE,
				_id,
				Severity::NOTIFICATION,
				_message + " took " + std::to_string(dt) + "us"
			);
		}

		popGroup();

		_active = false;
	}

private:
	GLuint _id;
	Source _source;
	std::string _message;

	bool _active = false;
	bool _measureTime = false;

	osg::Timer_t _start{};
};

class Scoped {
public:
	Scoped(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_state(id, message, source, measureTime) {
		_state.begin();
	}

	~Scoped() {
		_state.end();
	}

	Scoped(const Scoped&) = delete;
	Scoped& operator=(const Scoped&) = delete;

private:
	DebugGroupAnnotation _state;
};

enum class CameraDrawCallbackSlot {
	PRE_DRAW,
	POST_DRAW,
	FINAL_DRAW
};

namespace detail {
	inline osg::Camera::DrawCallback* getCameraDrawCallback(
		osg::Camera* camera,
		CameraDrawCallbackSlot slot
	) {
		switch(slot) {
			case CameraDrawCallbackSlot::PRE_DRAW: return camera->getPreDrawCallback();
			case CameraDrawCallbackSlot::POST_DRAW: return camera->getPostDrawCallback();
			case CameraDrawCallbackSlot::FINAL_DRAW: return camera->getFinalDrawCallback();
		}

		return nullptr;
	}

	inline void setCameraDrawCallback(
		osg::Camera* camera,
		CameraDrawCallbackSlot slot,
		osg::Camera::DrawCallback* cb
	) {
		switch(slot) {
			case CameraDrawCallbackSlot::PRE_DRAW:
				camera->setPreDrawCallback(cb);
				break;

			case CameraDrawCallbackSlot::POST_DRAW:
				camera->setPostDrawCallback(cb);
				break;

			case CameraDrawCallbackSlot::FINAL_DRAW:
				camera->setFinalDrawCallback(cb);
				break;
		}
	}
}

inline void appendCameraDrawCallback(
	osg::Camera* camera,
	CameraDrawCallbackSlot slot,
	osg::Camera::DrawCallback* cb
) {
	auto* existing = detail::getCameraDrawCallback(camera, slot);

	if(!existing) {
		detail::setCameraDrawCallback(camera, slot, cb);
		return;
	}

	if(auto* group = dynamic_cast<osgx::CameraDrawCallbacksGroup*>(existing)) {
		group->add(cb);
		return;
	}

	detail::setCameraDrawCallback(
		camera,
		slot,
		new osgx::CameraDrawCallbacksGroup({existing, cb})
	);
}

inline void prependCameraDrawCallback(
	osg::Camera* camera,
	CameraDrawCallbackSlot slot,
	osg::Camera::DrawCallback* cb
) {
	auto* existing = detail::getCameraDrawCallback(camera, slot);

	if(!existing) {
		detail::setCameraDrawCallback(camera, slot, cb);
		return;
	}

	detail::setCameraDrawCallback(
		camera,
		slot,
		new osgx::CameraDrawCallbacksGroup({cb, existing})
	);
}

class CameraDebugGroupBeginCallback: public osg::Camera::DrawCallback {
public:
	using Annotation = osg::ref_ptr<DebugGroupAnnotation>;

	CameraDebugGroupBeginCallback(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_annotation(new DebugGroupAnnotation(id, message, source, measureTime)) {}

	explicit CameraDebugGroupBeginCallback(Annotation annotation):
	_annotation(std::move(annotation)) {}

	void operator()(osg::RenderInfo&) const override {
		_annotation->begin();
	}

private:
	Annotation _annotation;
};

class CameraDebugGroupEndCallback: public osg::Camera::DrawCallback {
public:
	using Annotation = osg::ref_ptr<DebugGroupAnnotation>;

	CameraDebugGroupEndCallback(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_annotation(new DebugGroupAnnotation(id, message, source, measureTime)) {}

	explicit CameraDebugGroupEndCallback(Annotation annotation):
	_annotation(std::move(annotation)) {}

	void operator()(osg::RenderInfo&) const override {
		_annotation->end();
	}

private:
	Annotation _annotation;
};

// Phase 1: issues GL timestamp queries around the draw call and pushes the result
// into the per-context FrameAccumulator. Never blocks; the GPU pipeline is never
// stalled mid-submission. Pair with FinalDrawCallback on the camera for phase 2.
template<size_t N=DEFAULT_BUFFER_SIZE>
class DrawCallback: public osg::Drawable::DrawCallback {
public:
	using CPUBuffer = osgx::aring_buffer<decltype(osg::Timer::instance()->tick()), N>;

	DrawCallback(const std::string& name="", osg::Drawable::DrawCallback* cb=nullptr):
	_name(name),
	_cb(cb) {}

	DrawCallback(const std::string& name):
	DrawCallback(name, nullptr) {}

	DrawCallback(osg::Drawable::DrawCallback* cb):
	DrawCallback("", cb) {}

	const CPUBuffer& cpuBuffer() const { return _cpuBuffer; }

	virtual void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* drawable) const override {
		auto* ext = ri.getState()->get<osg::GLExtensions>();
		const auto& path = _name.empty() ? drawable->getName() : _name;
		const auto contextID = ri.getState()->getContextID();

		const auto start = osg::Timer::instance()->tick();

		if(ext && ext->glQueryCounter) {
			auto& acc = detail::_accumulators[contextID];
			auto e = acc.alloc(ext);

			e.path = path;

			ext->glQueryCounter(e.begin, GL_TIMESTAMP);

			if(!_cb) drawable->drawImplementation(ri);
			else _cb->drawImplementation(ri, drawable);

			ext->glQueryCounter(e.end, GL_TIMESTAMP);

			acc.push(std::move(e));
		}

		else {
			if(!_cb) drawable->drawImplementation(ri);
			else _cb->drawImplementation(ri, drawable);
		}

		_cpuBuffer.add(osg::Timer::instance()->tick() - start);
	}

protected:
	std::string _name;
	osg::ref_ptr<osg::Drawable::DrawCallback> _cb;
	mutable CPUBuffer _cpuBuffer;
};

enum class QueryMode { SYNC, ASYNC };

// Phase 2: install on a camera via setFinalDrawCallback(). After all drawables for
// that camera have submitted their timestamp queries, drains the accumulator.
// Accurate per-drawable GPU times with no cascading stalls.
//
// QueryMode::SYNC (default): blocks per-query on GL_QUERY_RESULT for the current
// frame. Fine for step-by-step viewers; may stall continuous rendering.
//
// QueryMode::ASYNC: drains the *previous* frame's results (already retired by the
// GPU) then swaps buffers. Zero meaningful stall; one-frame lag on results that the
// ring-buffer averaging makes irrelevant.
//
// Profiling is scoped to the camera; in a multipass/RTT setup each camera gets its
// own FinalDrawCallback and its own independent accumulator drain.
template<size_t N=DEFAULT_BUFFER_SIZE>
class FinalDrawCallback: public osg::Camera::DrawCallback {
public:
	explicit FinalDrawCallback(size_t printEvery=N, QueryMode mode=QueryMode::SYNC):
	_printEvery(printEvery),
	_mode(mode) {}

	void operator()(osg::RenderInfo& ri) const override {
		auto* ext = ri.getState()->get<osg::GLExtensions>();

		if(!ext || !ext->glGetQueryObjectui64v) return;

		const unsigned int frameNum = ri.getState()->getFrameStamp()
			? ri.getState()->getFrameStamp()->getFrameNumber()
			: 0u
		;

		auto& acc = detail::_accumulators[ri.getState()->getContextID()];

		if(_mode == QueryMode::ASYNC) acc.swap_and_drain(ext, _printEvery, frameNum);

		else acc.drain(ext, _printEvery, frameNum);
	}

private:
	size_t _printEvery;
	QueryMode _mode;
};

// Walks the scene graph and installs a DrawCallback on every Drawable, building a
// full scene-path string from NodeVisitor::getNodePath() for identification.
// Install FinalDrawCallback on the camera separately to complete the two-phase setup.
template<size_t N=DEFAULT_BUFFER_SIZE>
class DrawVisitor: public osg::NodeVisitor {
public:
	DrawVisitor():
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {}

	virtual void apply(osg::Geode& g) {
		traverse(g);
	}

	virtual void apply(osg::Drawable& d) {
		std::string path;

		for(auto* n : getNodePath()) {
			path += "/";
			path += n->getName().empty() ? n->className() : n->getName();
		}

		// In pre-Node-Drawable OSG, the drawable isn't pushed onto the NodePath.
		if(getNodePath().empty() || getNodePath().back() != &d) {
			path += "/";
			path += d.getName().empty() ? d.className() : d.getName();
		}

		auto dcb = new DrawCallback<N>(path, d.getDrawCallback());

		detail::print(" >> Setting dcb on ", path);

		d.setDrawCallback(dcb);

		traverse(d);
	}
};

/* static const auto DEBUG_EXTENSIONS = std::vector<std::string>{
	"GL_KHR_debug",
	"GL_ARB_debug_output",
	"GL_EXT_debug_marker",
	"GL_EXT_debug_label",
	"GL_AMD_debug_output",
}; */

class GraphicsOperation: public osg::GraphicsOperation {
public:
	GraphicsOperation():
	osg::Referenced(true),
	osg::GraphicsOperation("osgDebug::initialize Operation", false) {}

	virtual void operator()(osg::GraphicsContext* gc) {
		initialize(gc);
	}
};

#if 0
#define GL_DEBUG_CALLBACK_FUNCTION        0x8244
#define GL_DEBUG_CALLBACK_USER_PARAM      0x8245

#define GL_DEBUG_GROUP_STACK_DEPTH        0x826D

#define GL_DEBUG_LOGGED_MESSAGES          0x9145

#define GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH 0x8243

#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242

#define GL_DEBUG_SEVERITY_HIGH            0x9146
#define GL_DEBUG_SEVERITY_LOW             0x9148
#define GL_DEBUG_SEVERITY_MEDIUM          0x9147
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B

#define GL_DEBUG_SOURCE_API               0x8246
#define GL_DEBUG_SOURCE_APPLICATION       0x824A
#define GL_DEBUG_SOURCE_OTHER             0x824B
#define GL_DEBUG_SOURCE_SHADER_COMPILER   0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY       0x8249
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM     0x8247

#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_ERROR               0x824C
#define GL_DEBUG_TYPE_MARKER              0x8268
#define GL_DEBUG_TYPE_OTHER               0x8251
#define GL_DEBUG_TYPE_PERFORMANCE         0x8250
#define GL_DEBUG_TYPE_POP_GROUP           0x826A
#define GL_DEBUG_TYPE_PORTABILITY         0x824F
#define GL_DEBUG_TYPE_PUSH_GROUP          0x8269
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  0x824E
#endif

class Viewer: public osgViewer::Viewer {
public:
	void frame(double st=USE_REFERENCE_TIME) override {
		if(_done) return;

		pushGroup(1, "osgDebug::Viewer::frame()");

		ensureInitialized();

		advance(st);

		renderFrame();

		popGroup();
	}

	int run() override {
		while(!done()) frame();

		return 0;
	}

	// This method is provided simply for the documentation below; the steps indicated below are
	// exactly what the typical `osgViewer::Viewer::renderingTraversals()` method does.
	void renderingTraversals() override {
		// - Calculate frame time/number.
		// - Collect stats (if enabled).
		// - Iterate over getScenes().
		//   - Call scene.DatabasePager.signalBeginFrame().
		//   - Call scene.ImagePager.signalBeginFrame().
		//   - Compute bounds of scene.
		// - Iterate over getCameras().
		//   - Call camera.getRenderer().cull().
		// - Iterate over getContexts(), defined in subclass (Viewer/CompositeViewer).
		//   - Make context thread active.
		//   - Call context.runOperations().
		//     - Iterate over all context cameras.
		//       - Call camera.getRenderer()(context).
		//         - osgViewer::Renderer calls either cull_draw() or draw(), FINALLY
		//           leading to our Drawable! The order of function call is:
		//             - SceneView::draw()
		//             - RenderBin::draw()
		// - Iterate over getContexts().
		//   - Make context thread active.
		//   - Call context.swapBuffers().
		// - Iterate over getScenes().
		//   - Call scene.DatabasePager.signalEndFrame().
		//   - Call scene.ImagePager.signalEndFrame().
		// - Update stats (if enabled).
		ViewerBase::renderingTraversals();
	}

protected:
	void ensureInitialized() {
		if(_firstFrame) {
			viewerInit();

			if(!isRealized()) realize();

			_firstFrame = false;
		}
	}

	virtual void renderFrame() {
		auto [_et, et] = osgx::call([this]() { eventTraversal(); });
		auto [_ut, ut] = osgx::call([this]() { updateTraversal(); });
		auto [_rt, rt] = osgx::call([this]() { renderingTraversals(); });

		std::ostringstream msg;

		msg
			<< "frame #" << getFrameStamp()->getFrameNumber() << " ["
			<< "event=" << et
			<< ", update=" << ut
			<< ", rendering=" << rt
			<< "](us)"
		;

		messageInsert(Type::PERFORMANCE, 0, Severity::NOTIFICATION, msg.str());
	}
};

class FrameByFrameViewer: public Viewer {
public:
	class EventHandler: public osgGA::GUIEventHandler {
	public:
		bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
			auto* viewer = dynamic_cast<FrameByFrameViewer*>(&aa);

			if(viewer && ea.getEventType() == osgGA::GUIEventAdapter::KEYUP) {
				if(ea.getKey() == 'n') {
					viewer->requestRender();

					return true;
				}
			}

			return false;
		}
	};

	FrameByFrameViewer():
	_renderKeyHandler(new EventHandler()) {
		addEventHandler(_renderKeyHandler);
	}

	void requestRender() {
		_render.store(true, std::memory_order_release);
	}

	int run() override {
		while(!done()) {
			ensureInitialized();
			installTraceHintCallbacks();

			advance();
			eventTraversal();

			if(_render.load(std::memory_order_acquire)) {
				_count++;

				getFrameStamp()->setFrameNumber(_count);

				double wallTime = osg::Timer::instance()->delta_s(
					_startTick,
					osg::Timer::instance()->tick()
				);

				detail::print(
					"FrameByFrameViewer render #", _count,
					" (wall-clock ", wallTime, "s)"
				);

				auto [_ut, ut] = osgx::call([this]() { updateTraversal(); });
				detail::print("Update took ", ut, "us");

				renderingTraversals();

				_render.store(false, std::memory_order_release);
			}

			OpenThreads::Thread::microSleep(_POLL_INTERVAL_US);
		}

		return 0;
	}

private:
	void installTraceHintCallbacks() {
		if(_traceHintCallbacksInstalled) return;

		auto* camera = getCamera();
		auto frameAnnotation = osgx::make_ref<DebugGroupAnnotation>(
			0,
			"Frame",
			Source::APPLICATION,
			true
		);
		auto renderAnnotation = osgx::make_ref<DebugGroupAnnotation>(
			2,
			"Render",
			Source::APPLICATION,
			true
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::PRE_DRAW,
			new CameraDebugGroupBeginCallback(frameAnnotation)
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::PRE_DRAW,
			new CameraDebugGroupBeginCallback(renderAnnotation)
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::FINAL_DRAW,
			new CameraDebugGroupEndCallback(renderAnnotation)
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::FINAL_DRAW,
			new CameraDebugGroupEndCallback(frameAnnotation)
		);

		_traceHintCallbacksInstalled = true;
	}

	static constexpr unsigned int _POLL_INTERVAL_US = 100000;

	osg::ref_ptr<EventHandler> _renderKeyHandler;

	std::atomic<bool> _render{true};
	bool _traceHintCallbacksInstalled = false;

	osg::Timer_t _startTick = osg::Timer::instance()->tick();
	unsigned int _count = 0;
};

}
