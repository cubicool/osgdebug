#pragma once

#include "osgx.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/GLExtensions>
#include <osg/Drawable>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

// TODO: Add notification mirroring of debug calls (to std::cout, osg::notify, etc).
// TODO: Make some macro wrappers for pushGroup/insertMessage that use __FUNCTION__, __FILE__, etc.
// TODO: pushGroup/insertMessage accept an "id", which we probably should manage automatically.

namespace osgDebug {

using namespace osgx::literals;

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

enum class QueryMode {
	ASYNC, // Default: non-blocking, harvest previous frame's results.
	SYNC   // Frame-by-frame: blocking read of this frame's results.
};

namespace detail {
	inline constexpr void print(const auto&... args) {
		// ((std::cout << args), ...) << std::endl;
		((osg::notify(osg::NOTICE) << args), ...) << std::endl;
	}

	// TODO: using = std::function<void(GLenum, GLuint, GLsizei, const char*)>;
	//
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

template<size_t N=120, size_t C=N>
class DrawCallback: public osg::Drawable::DrawCallback {
public:
	using CPUBuffer = osgx::aring_buffer<decltype(osg::Timer::instance()->tick()), N>;
	using GPUBuffer = osgx::aring_buffer<GLuint64, N>;

	struct PendingQuery {
		GLuint begin = 0;
		GLuint end = 0;
	};

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

	DrawCallback(QueryMode m, const std::string& name="", osg::Drawable::DrawCallback* cb=nullptr):
	_name(name),
	_cb(cb),
	_queryMode(m) {
	}

	// Accessors for external consumers (e.g. stats overlays, logging)
	const CPUBuffer& cpuBuffer() const { return _cpuBuffer; }
	const GPUBuffer& gpuBuffer() const { return _gpuBuffer; }

	void setQueryMode(QueryMode m) { _queryMode = m; }

	virtual void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* drawable) const {
		auto* ext = ri.getState()->get<osg::GLExtensions>();

		// TODO: Make using `path` a toggle between getUserValue("path") and getName().
		auto path = drawable->getName();

		// --- Harvest last frame's GPU query (non-blocking, ASYNC only) ---
		if(_queryMode == QueryMode::ASYNC && _pending && ext && ext->glGetQueryObjectui64v) {
			GLint available = 0;

			ext->glGetQueryObjectiv(_pending->begin, GL_QUERY_RESULT_AVAILABLE, &available);

			if(available) {
				GLuint64 t0 = 0, t1 = 0;

				ext->glGetQueryObjectui64v(_pending->begin, GL_QUERY_RESULT, &t0);
				ext->glGetQueryObjectui64v(_pending->end, GL_QUERY_RESULT, &t1);

				const GLuint64 gpuNs = t1 - t0;

				_gpuBuffer.add(gpuNs);

				_samplesSincePrint++;

				if(_samplesSincePrint >= C) {
					_samplesSincePrint = 0;

					detail::print(
						" >> [", path, "] GPU: ", gpuNs / 1000u, "us",
						" | avg: ", _gpuBuffer.average(C) / 1000u, "us",
						" (", _gpuBuffer.size(), " samples / ", C, " averaged) Frame: ",
						ri.getState()->getFrameStamp()->getFrameNumber()
					);
				}
			}

			_freeList.push_back(*_pending);

			_pending.reset();
		}

		// --- Issue GPU timestamp queries + draw ---
		if(ext && ext->glQueryCounter) {
			PendingQuery q;

			if(!_freeList.empty()) {
				q = _freeList.back();

				_freeList.pop_back();
			}

			else {
				ext->glGenQueries(1, &q.begin);
				ext->glGenQueries(1, &q.end);
			}

			ext->glQueryCounter(q.begin, GL_TIMESTAMP);

			const auto start = osg::Timer::instance()->tick();

			if(!_cb) drawable->drawImplementation(ri);
			else _cb->drawImplementation(ri, drawable);

			const auto stop = osg::Timer::instance()->tick();

			ext->glQueryCounter(q.end, GL_TIMESTAMP);

			// SYNC: blocking read — caller ensures glFinish() before the next frame.
			if(_queryMode == QueryMode::SYNC && ext->glGetQueryObjectui64v) {
				GLuint64 t0 = 0, t1 = 0;

				ext->glGetQueryObjectui64v(q.begin, GL_QUERY_RESULT, &t0);
				ext->glGetQueryObjectui64v(q.end, GL_QUERY_RESULT, &t1);

				const GLuint64 gpuNs = t1 - t0;

				_gpuBuffer.add(gpuNs);

				detail::print(
					" >> [", path, "] GPU: ", gpuNs / 1000u, "us",
					" Frame: ", ri.getState()->getFrameStamp()->getFrameNumber()
				);

				_freeList.push_back(q);
			}

			// ASYNC: defer harvest to the next frame.
			else {
				_pending = q;
			}

			_cpuBuffer.add(stop - start);
		}
	}

protected:
	std::string _name;

	osg::ref_ptr<osg::Drawable::DrawCallback> _cb;

	QueryMode _queryMode = QueryMode::ASYNC;

	mutable CPUBuffer _cpuBuffer;
	mutable GPUBuffer _gpuBuffer;

	mutable std::optional<PendingQuery> _pending;
	mutable std::vector<PendingQuery> _freeList;

	mutable size_t _samplesSincePrint = 0;
};

// TODO: This should keep track of the parented "path" and include it with debug output!
template<size_t N=120, size_t C=N>
class DrawVisitor: public osg::NodeVisitor {
public:
	DrawVisitor(QueryMode m = QueryMode::ASYNC):
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
	_queryMode(m) {
	}

	void setQueryMode(QueryMode m) { _queryMode = m; }

	virtual void apply(osg::Geode& g) {
		traverse(g);
	}

	virtual void apply(osg::Drawable& d) {
		auto dcb = new DrawCallback<N, C>(d.getName(), d.getDrawCallback());

		dcb->setQueryMode(_queryMode);

		detail::print(" >> Setting dcb on ", d.getName());

		d.setDrawCallback(dcb);

		traverse(d);
	}

private:
	QueryMode _queryMode = QueryMode::ASYNC;
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

	FrameByFrameViewer(unsigned int sleep=100000):
	_renderKeyHandler(new EventHandler()),
	_sleep(sleep) {
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

			OpenThreads::Thread::microSleep(_sleep);
		}

		return 0;
	}

private:
	osg::ref_ptr<EventHandler> _renderKeyHandler;

	std::atomic<bool> _render{true};

	osg::Timer_t _startTick = osg::Timer::instance()->tick();

	// The value to sleep between each render frame check; same as the value that would
	// be passed to the Unix microsleep() function.
	unsigned int _sleep;
	unsigned int _count = 0;
};

}
