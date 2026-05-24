#pragma once

#include "osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GLExtensions>
#include <osg/Drawable>
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
	HIGH = 0x9146,
	LOW = 0x9148,
	MEDIUM = 0x9147,
	NOTIFICATION = 0x826B
};

// TODO: The default "source" (for pushGroup/messageInsert) is APPLICATION; does this make sense?
// What are the others even FOR!?
enum class Source: GLenum {
	API = 0x8246,
	APPLICATION = 0x824A,
	OTHER = 0x824B,
	SHADER_COMPILER = 0x8248,
	THIRD_PARTY = 0x8249,
	WINDOW_SYSTEM = 0x8247
};

enum class Type: GLenum {
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
	inline constexpr void print(const auto&... args) {
		((osg::notify(osg::NOTICE) << args), ...) << std::endl;
	}

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPushDebugGroup.xhtml
	using glPushDebugGroupFunc = void(*)(GLenum, GLuint, GLsizei, const char*);

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPopDebugGroup.xhtml
	using glPopDebugGroupFunc = void(*)();

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageInsert.xhtml
	using glDebugMessageInsertFunc = void(*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const char*);

	inline glPushDebugGroupFunc _pushGroup = nullptr;
	inline glPopDebugGroupFunc _popGroup = nullptr;
	inline glDebugMessageInsertFunc _messageInsert = nullptr;

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

// TODO: Add a check for whether we're already initialized.
inline void initialize(osg::GraphicsContext* gc) {
	if(osg::isGLExtensionSupported(gc->getState()->getContextID(), "GL_KHR_debug")) {
		detail::setupFunction("glPushDebugGroup", &detail::_pushGroup);
		detail::setupFunction("glPopDebugGroup", &detail::_popGroup);
		detail::setupFunction("glDebugMessageInsert", &detail::_messageInsert);
	}
}

class Scoped {
public:
	Scoped(
		GLuint id,
		std::string_view message,
		Source source=Source::APPLICATION,
		bool measureTime=false
	):
	_id(id),
	_source(source),
	_message(message),
	_measureTime(measureTime) {
		_active = detail::_pushGroup != nullptr;

		if(_active) pushGroup(_source, _id, std::string(_message));

		if(_measureTime) _start = osg::Timer::instance()->tick();
	}

	~Scoped() {
		if(!_active) return;

		if(_measureTime) {
			auto stop = osg::Timer::instance()->tick();
			auto dt = stop - _start;

			messageInsert(
				Type::PERFORMANCE,
				_id,
				Severity::NOTIFICATION,
				std::string(_message) + " took " + std::to_string(dt) + "us"
			);
		}

		popGroup();
	}

	Scoped(const Scoped&) = delete;
	Scoped& operator=(const Scoped&) = delete;

private:
	GLuint _id;
	Source _source;
	std::string _message;

	bool _active = false;
	bool _measureTime = false;

	osg::Timer_t _start{};
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
	_cb(cb) {
	}

	DrawCallback(const std::string& name):
	DrawCallback(name, nullptr) {
	}

	DrawCallback(osg::Drawable::DrawCallback* cb):
	DrawCallback("", cb) {
	}

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
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {
	}

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
	osg::GraphicsOperation("GraphicsOperation", false) {
	}

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

				{
					Scoped scoped(0, "Frame", Source::APPLICATION, true);

					{
						Scoped update(1, "Update", Source::APPLICATION, true);
						updateTraversal();
					}

					{
						Scoped render(2, "Render", Source::APPLICATION, true);
						renderingTraversals();
					}
				}

				_render.store(false, std::memory_order_release);
			}

			OpenThreads::Thread::microSleep(_POLL_INTERVAL_US);
		}

		return 0;
	}

private:
	static constexpr unsigned int _POLL_INTERVAL_US = 100000;

	osg::ref_ptr<EventHandler> _renderKeyHandler;

	std::atomic<bool> _render{true};

	osg::Timer_t _startTick = osg::Timer::instance()->tick();
	unsigned int _count = 0;
};

}
