#pragma once

#include "osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GLExtensions>
#include <osg/Drawable>
#include <osg/Camera>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#ifdef OSGDEBUG_IMGUI
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#endif

#include <unordered_map>
#include <functional>
#include <sstream>

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

	// All osgDebug log output - internal diagnostics, GL annotation mirrors, driver
	// messages - flows through this single sink. Replace it to redirect to spdlog,
	// Qt logging, an ImGui scrollback, etc. Default: osg::notify(NOTICE).
	inline std::function<void(std::string_view)> _sink = [](std::string_view s) {
		osg::notify(osg::NOTICE) << s << std::endl;
	};

	inline void notify(const auto&... args) {
		std::ostringstream oss;
		((oss << args), ...);
		_sink(oss.str());
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
		const auto src = static_cast<Source>(source);
		const auto typ = static_cast<Type>(type);
		const auto sev = static_cast<Severity>(severity);

		std::string msg;

		if(message) {
			if(length >= 0) msg.assign(message, static_cast<size_t>(length));
			else msg = message;
		}

		std::ostringstream oss;

		oss << "osgDebug | source=" << toString(src)
			<< " type=" << toString(typ)
			<< " severity=" << toString(sev)
			<< " id=" << id
			<< " | " << msg;

		_sink(oss.str());
	}

	inline void pushGroup(Source source, GLuint id, const std::string& message) {
		if(!_pushGroup) return;

		_pushGroup(
			static_cast<std::underlying_type_t<Source>>(source),
			id,
			-1,
			message.c_str()
		);

		notify("osgDebug::pushGroup | ", message);
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

		notify("osgDebug::messageInsert | ", message);
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

	inline void installDefaultCallback(bool synchronous=true) {
		if(!_messageCallback) return;

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

			notify("osgDebug | Bound function '", name, "' to @", (void*)(*func));
		}

		else notify("osgDebug | FAILED to bind '", name, "'");
	}

	// Per-context accumulator for two-phase GPU timing. ProfilerCallback (phase 1) pushes
	// pending timestamp query pairs here without blocking. ProfilerFinalCallback (phase 2)
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
					notify(
						"osgDebug::Profiler | [", e.path, "] GPU: ", gpuNs / 1000u, "us",
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

inline void installDefaultCallback(bool synchronous=true) {
	detail::installDefaultCallback(synchronous);
}

// Replace the log sink for all osgDebug output (internal diagnostics, GL annotation
// mirrors, driver messages). The default writes to osg::notify(NOTICE).
inline void setSink(std::function<void(std::string_view)> sink) {
	detail::_sink = std::move(sink);
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
}

// Annotations are camera-scoped: a PRE_DRAW/FINAL_DRAW pair brackets everything that camera
// renders. Sub-groups within a single camera's scene graph cannot be individually annotated this
// way; for per-group bracketing, each group needs its own camera (RTT). For per-drawable bracketing
// use Scoped inside a DrawCallback instead.
//
// Choosing the right tool:
//
// Scoped - begin and end are in the SAME scope (e.g. inside a single drawImplementation call).
// RAII; use on the stack.
//
// AnnotationGroup - begin and end must live in SEPARATE callbacks. Its sole purpose is to share
// state across AnnotationBeginCallback (PRE_DRAW) and AnnotationEndCallback (FINAL_DRAW) so the
// push/pop remain matched across the camera's frame boundary. Not a general-purpose annotation
// type.
//
// All calls require a current GL context; never call begin()/end() from application-side C++ code
// outside a draw callback.
class AnnotationGroup: public osg::Referenced {
public:
	AnnotationGroup(
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

// RAII wrapper: opens the group on construction, closes on destruction.
// Safe on the stack inside a draw callback; do not use from application C++ code.
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
	AnnotationGroup _state;
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
				camera->setPreDrawCallback(cb); break;

			case CameraDrawCallbackSlot::POST_DRAW:
				camera->setPostDrawCallback(cb); break;

			case CameraDrawCallbackSlot::FINAL_DRAW:
				camera->setFinalDrawCallback(cb); break;
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

// Fires AnnotationGroup::begin() from a camera draw slot (GL context current).
class AnnotationBeginCallback: public osg::Camera::DrawCallback {
public:
	using Annotation = osg::ref_ptr<AnnotationGroup>;

	AnnotationBeginCallback(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_annotation(new AnnotationGroup(id, message, source, measureTime)) {}

	explicit AnnotationBeginCallback(Annotation annotation):
	_annotation(std::move(annotation)) {}

	void operator()(osg::RenderInfo&) const override {
		_annotation->begin();
	}

private:
	Annotation _annotation;
};

// Fires AnnotationGroup::end() from a camera draw slot (GL context current).
class AnnotationEndCallback: public osg::Camera::DrawCallback {
public:
	using Annotation = osg::ref_ptr<AnnotationGroup>;

	AnnotationEndCallback(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_annotation(new AnnotationGroup(id, message, source, measureTime)) {}

	explicit AnnotationEndCallback(Annotation annotation):
	_annotation(std::move(annotation)) {}

	void operator()(osg::RenderInfo&) const override {
		_annotation->end();
	}

private:
	Annotation _annotation;
};

// GPU/glGenQueries Profiling
//
// Two-phase design: ProfilerCallback (phase 1) wraps each drawable and issues glQueryCounter
// begin/end without blocking. ProfilerFinalCallback (phase 2) drains the accumulated results after
// the camera finishes rendering.
//
// Usage:
//
// root->accept(osgDebug::ProfilerVisitor());
// appendCameraDrawCallback(cam, FINAL_DRAW, new osgDebug::ProfilerFinalCallback());
enum class QueryMode { SYNC, ASYNC };

// Phase 1: wrap a drawable, bracket its draw with GL timestamp queries.
// Never blocks; safe in any threading model.
template<size_t N=DEFAULT_BUFFER_SIZE>
class ProfilerCallback: public osg::Drawable::DrawCallback {
public:
	using CPUBuffer = osgx::aring_buffer<decltype(osg::Timer::instance()->tick()), N>;

	ProfilerCallback(const std::string& name="", osg::Drawable::DrawCallback* cb=nullptr):
	_name(name),
	_cb(cb) {}

	ProfilerCallback(const std::string& name):
	ProfilerCallback(name, nullptr) {}

	ProfilerCallback(osg::Drawable::DrawCallback* cb):
	ProfilerCallback("", cb) {}

	const CPUBuffer& cpuBuffer() const { return _cpuBuffer; }

	virtual void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* drawable) const override {
		auto* ext = ri.getState()->get<osg::GLExtensions>();
		const auto& path = _name.empty() ? drawable->getName() : _name;
		const auto contextID = ri.getState()->getContextID();

		const auto start = osg::Timer::instance()->tick();

		Scoped scoped(0, path);

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

// Phase 2: install on a camera via FINAL_DRAW slot to drain the FrameAccumulator.
//
// QueryMode::SYNC (default): blocks per-query on GL_QUERY_RESULT for the current
// frame. Fine for step-by-step viewers; may stall continuous rendering.
//
// QueryMode::ASYNC: drains the previous frame's results (already retired by the
// GPU) then swaps buffers. Zero meaningful stall; one-frame lag on results that the
// ring-buffer averaging makes irrelevant.
template<size_t N=DEFAULT_BUFFER_SIZE>
class ProfilerFinalCallback: public osg::Camera::DrawCallback {
public:
	explicit ProfilerFinalCallback(size_t printEvery=N, QueryMode mode=QueryMode::SYNC):
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

// Walks the scene graph and installs a ProfilerCallback on every Drawable.
// Install ProfilerFinalCallback on the camera separately to complete setup.
template<size_t N=DEFAULT_BUFFER_SIZE>
class ProfilerVisitor: public osg::NodeVisitor {
public:
	ProfilerVisitor():
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

		auto dcb = new ProfilerCallback<N>(path, d.getDrawCallback());

		detail::notify("osgDebug::ProfilerVisitor | Setting ProfilerCallback on ", path);

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

// An osgViewer::Viewer subclass for apitrace-friendly debugging. Press 'n' to
// advance one frame; the viewer idles between keystrokes so apitrace captures
// never flood with unintended frames.
//
// On first render, installs AnnotationBeginCallback / AnnotationEndCallback on
// the camera so every rendered frame appears in apitrace as:
//
// Frame { [all draw calls for this frame] }
//
// Any other object with its own RTT camera (e.g. osgSlug::Atlas) can install
// its own annotation callbacks and they will nest naturally inside this group.
class FrameByFrameViewer: public osgViewer::Viewer {
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
			_ensureInitialized();
			_installAnnotationCallbacks();

			advance();
			eventTraversal();

			if(_render.load(std::memory_order_acquire)) {
				_count++;

				getFrameStamp()->setFrameNumber(_count);

				double wallTime = osg::Timer::instance()->delta_s(
					_startTick,
					osg::Timer::instance()->tick()
				);

				detail::notify(
					"osgDebug::FrameByFrameViewer | render #", _count,
					" @", wallTime, "s"
				);

				auto [_ut, ut] = osgx::call([this]() { updateTraversal(); });

				detail::notify("osgDebug::FrameByFrameViewer | Update took ", ut, "us");

				// Below are exactly what the typical `osgViewer::Viewer::renderingTraversals()`
				// method does.
				//
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
				renderingTraversals();

				_render.store(false, std::memory_order_release);
			}

			OpenThreads::Thread::microSleep(_POLL_INTERVAL_US);
		}

		return 0;
	}

private:
	void _ensureInitialized() {
		if(_firstFrame) {
			viewerInit();

			if(!isRealized()) realize();

			_firstFrame = false;
		}
	}

	void _installAnnotationCallbacks() {
		if(_annotationCallbacksInstalled) return;

		auto* camera = getCamera();

		auto frameAnnotation = osgx::make_ref<AnnotationGroup>(
			0,
			"Frame",
			Source::APPLICATION,
			true
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::PRE_DRAW,
			new AnnotationBeginCallback(frameAnnotation)
		);

		appendCameraDrawCallback(
			camera,
			CameraDrawCallbackSlot::FINAL_DRAW,
			new AnnotationEndCallback(frameAnnotation)
		);

		_annotationCallbacksInstalled = true;
	}

	static constexpr unsigned int _POLL_INTERVAL_US = 100000;

	osg::ref_ptr<EventHandler> _renderKeyHandler;

	std::atomic<bool> _render{true};
	bool _firstFrame = true;
	bool _annotationCallbacksInstalled = false;
	osg::Timer_t _startTick = osg::Timer::instance()->tick();
	unsigned int _count = 0;
};

#ifdef OSGDEBUG_IMGUI
// ImGuiHandler - drop-in osgGA::GUIEventHandler that owns the ImGui lifecycle.
//
// Installs pre/post draw callbacks on the camera on the first OSG event.
// Requires SingleThreaded viewer (ImGui context is per-thread).
//
// Set onDraw to emit ImGui calls; they run inside an active frame.
class ImGuiHandler: public osgGA::GUIEventHandler {
public:
	std::function<void(osg::RenderInfo&)> onDraw;

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(!_initialized) {
			if(auto* view = aa.asView()) {
				auto* cam = view->getNumSlaves() > 0
					? view->getSlave(0)._camera.get()
					: view->getCamera()
				;

				cam->setPreDrawCallback(new PreDraw(*this));
				cam->setPostDrawCallback(new PostDraw(*this));

				_initialized = true;
			}

			return false;
		}

		if(_firstFrame || ea.getHandled()) return false;

		ImGuiIO& io = ImGui::GetIO();

		switch(ea.getEventType()) {
		case osgGA::GUIEventAdapter::KEYDOWN:
		case osgGA::GUIEventAdapter::KEYUP: {
			bool down = ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
			int c = ea.getKey();

			if(io.WantCaptureKeyboard && down && c >= 32 && c < 127) io.AddInputCharacter(
				static_cast<unsigned int>(c)
			);

			return io.WantCaptureKeyboard;
		}

		case osgGA::GUIEventAdapter::PUSH:
			io.AddMousePosEvent(ea.getX(), io.DisplaySize.y - ea.getY());

			if(ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) io.AddMouseButtonEvent(
				ImGuiMouseButton_Left,
				true
			);

			else if(ea.getButton() == osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON) {
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
			}

			return io.WantCaptureMouse;

		case osgGA::GUIEventAdapter::RELEASE:
			io.AddMousePosEvent(ea.getX(), io.DisplaySize.y - ea.getY());

			if(ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) io.AddMouseButtonEvent(
				ImGuiMouseButton_Left,
				false
			);

			else if(ea.getButton() == osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON) {
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
			}

			return io.WantCaptureMouse;

		case osgGA::GUIEventAdapter::DRAG:
		case osgGA::GUIEventAdapter::MOVE:
			io.AddMousePosEvent(ea.getX(), io.DisplaySize.y - ea.getY());

			return io.WantCaptureMouse;

		case osgGA::GUIEventAdapter::SCROLL:
			io.AddMouseWheelEvent(
				0.0f,
				ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP ? 1.0f : -1.0f
			);

			return io.WantCaptureMouse;

		default:
			break;
		}

		return false;
	}

private:
	bool _initialized = false;
	bool _firstFrame = true;
	double _time = 0.0;

	void newFrame(osg::RenderInfo& ri) {
		if(_firstFrame) {
			ImGui::CreateContext();
			ImGui_ImplOpenGL3_Init();

			_firstFrame = false;
		}

		ImGui_ImplOpenGL3_NewFrame();

		auto* traits = ri.getCurrentCamera()->getGraphicsContext()->getTraits();

		ImGuiIO& io = ImGui::GetIO();

		io.DisplaySize = ImVec2(
			static_cast<float>(traits->width),
			static_cast<float>(traits->height)
		);

		double t = ri.getView()->getFrameStamp()->getSimulationTime();

		io.DeltaTime = static_cast<float>(t - _time) + 0.0000001f;

		_time = t;

		ImGui::NewFrame();
	}

	void render(osg::RenderInfo& ri) {
		if(onDraw) onDraw(ri);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	struct PreDraw: public osg::Camera::DrawCallback {
		ImGuiHandler& _h;

		explicit PreDraw(ImGuiHandler& h): _h(h) {}

		void operator()(osg::RenderInfo& ri) const override { _h.newFrame(ri); }
	};

	struct PostDraw: public osg::Camera::DrawCallback {
		ImGuiHandler& _h;

		explicit PostDraw(ImGuiHandler& h): _h(h) {}

		void operator()(osg::RenderInfo& ri) const override { _h.render(ri); }
	};
};
#endif

}
