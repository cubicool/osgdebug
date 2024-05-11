#include <osg/GLExtensions>
#include <osg/Drawable>
#include <osgViewer/Viewer>

// TODO: Add notification mirroring of debug calls.

namespace osgDebug {

enum class Severity: GLenum {
	HIGH = 0x9146,
	LOW = 0x9148,
	MEDIUM = 0x9147,
	NOTIFICATION = 0x826B
};

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
	PERFORMANCE= 0x8250,
	POP_GROUP = 0x826A,
	PORTABILITY = 0x824F,
	PUSH_GROUP = 0x8269,
	UNDEFINED_BEHAVIOR = 0x824E
};

namespace internal {
	// TODO: using = std::function<void(GLenum, GLuint, GLsizei, const char*)>;
	//
	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPushDebugGroup.xhtml
	using glPushDebugGroupFunc = void(*)(GLenum, GLuint, GLsizei, const char*);

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPopDebugGroup.xhtml
	using glPopDebugGroupFunc = void(*)();

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageInsert.xhtml
	using glDebugMessageInsertFunc = void(*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const char*);

	glPushDebugGroupFunc _pushGroup = nullptr;
	glDebugMessageInsertFunc _messageInsert = nullptr;
	glPopDebugGroupFunc _popGroup = nullptr;

	void pushGroup(Source source, GLuint id, const std::string& message) {
		if(!_pushGroup) return;

		_pushGroup(
			static_cast<std::underlying_type_t<Source>>(source),
			id,
			-1,
			message.c_str()
		);
	}

	void popGroup() {
		if(!_popGroup) return;

		_popGroup();
	}

	void messageInsert(
		GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		const std::string& message
	) {
		if(!_messageInsert) return;

		_messageInsert(source, type, id, severity, -1, message.c_str());
	}
}

inline void pushGroup(Source source, GLuint id, const std::string& message) {
	internal::pushGroup(source, id, message);
}

inline void pushGroup(GLuint id, const std::string& message) {
	pushGroup(Source::APPLICATION, id, message);
}

inline void popGroup() {
	internal::popGroup();
}

inline void messageInsert(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	const std::string& message
) {
	internal::messageInsert(source, type, id, severity, message);
}

inline void messageInsert(
	Source source,
	Type type,
	GLuint id,
	Severity severity,
	const std::string& message
) {
	messageInsert(
		static_cast<std::underlying_type_t<Source>>(source),
		static_cast<std::underlying_type_t<Type>>(type),
		id,
		static_cast<std::underlying_type_t<Severity>>(severity),
		message
	);
}

#if 0
// TODO: osg::Drawable::DrawCallback
// TODO: osg::DrawableUpdateCallback
// TODO: osg::DrawableCullCallback
template<typename Node>
class NodeCallback: public osg::NodeCallback {
public:
	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		// TODO: Helpers to ensure cast.
		Node* n = dynamic_cast<Node*>(node);

		// TODO: Helpers to ensure what KIND of Visitor.
		// osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(nv);

		// TODO: Pre-actions...
		traverse(node, nv);
		// TODO: Post-actions...
	}
};
#endif

// TODO: Create a Visitor that will install DrawCallbacks on ALL Drawables; if they already HAVE a
// DrawCallback, store it to call manually; otherwise, set it directly.
class DrawCallback: public osg::Drawable::DrawCallback {
public:
	virtual void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* drawable) const {
		// pushGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, __FUNCTION__);
		// messageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, 0, GL_DEBUG_SEVERITY_MEDIUM, -1, "bla bla");
		messageInsert(Source::APPLICATION, Type::OTHER, 0, Severity::MEDIUM, "bla bla");
		// popGroup();

		std::cout << " >> " << drawable->getName() << " draw call back - pre drawImplementation" << drawable << std::endl;

		drawable->drawImplementation(ri);

		std::cout << " >> " << drawable->getName() << " draw call back - post drawImplementation" << drawable << std::endl;

		// const_cast<osg::Drawable*>(drawable)->dirtyGLObjects();
		std::cout << _time(ri) << std::endl;
	}

protected:
	constexpr auto _time(const osg::RenderInfo& ri) const {
		return ri.getState()->getFrameStamp()->getReferenceTime();
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
		if(osg::isGLExtensionSupported(gc->getState()->getContextID(), "GL_KHR_debug")) {
			_setupFunction("glPushDebugGroup", &internal::_pushGroup);
			_setupFunction("glPopDebugGroup", &internal::_popGroup);
			_setupFunction("glDebugMessageInsert", &internal::_messageInsert);
		}
	}

private:
	template<typename T>
	void _setupFunction(const std::string& name, T* func) {
		void* f = osg::getGLExtensionFuncPtr(name.c_str());

		if(f) {
			*func = reinterpret_cast<T>(f);

			OSG_NOTICE << " >> Bound function '" << name << "' to @" << (void*)(*func) << std::endl;
		}

		else OSG_NOTICE << " >> FAILED to bind '" << name << "'" << std::endl;
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

// TODO: Create a new UpdateVisitor and set it on FrameByFrameViewer->setUpdateVisitor; our
// UpdateVisitor should have an API for fetching top-level stuff.

// TODO: Should this inherit from a tempalted Viewer instead?
// template<typename T=osgViewer::Viewer>
class FrameByFrameViewer: public osgViewer::Viewer {
public:
	class RenderKeyHandler: public osgGA::GUIEventHandler {
	public:
		bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
			FrameByFrameViewer* viewer = dynamic_cast<FrameByFrameViewer*>(&aa);

			if(viewer && ea.getEventType() == osgGA::GUIEventAdapter::KEYUP) {
				if(ea.getKey() == 'n') {
					viewer->_render = true;

					return true;
				}
			}

			return false;
		}
	};

	friend class RenderKeyHandler;

	FrameByFrameViewer(unsigned int sleep=100000):
	_renderKeyHandler(new RenderKeyHandler()),
	_sleep(sleep) {
		addEventHandler(_renderKeyHandler);
	}

	virtual void frame(double st=USE_REFERENCE_TIME) {
		if(_done) return;

		if(_firstFrame) {
			viewerInit();

			if(!isRealized()) realize();

			_firstFrame = false;
		}

		advance(st);
		eventTraversal();
		updateTraversal();

		if(_render) {
			OSG_NOTICE
				<< "FrameByFrameViewer frame #" << _count
				<< " at simulated time " << st
				<< std::endl
			;

			renderingTraversals();

			_render = false;

			_count++;
		}
	}

	virtual int run() {
		// TODO: Use a better mechanism (osg::Timer) to manage "simulation time."
		// auto timer = osg::Timer::instance();
		double st = 0.0;

		/* auto wrap = [this](auto str, auto func) {
			OSG_NOTICE << " >> " << str << std::endl;

			func();

			OSG_NOTICE << " << " << str << std::endl;
		}; */

		while(!done()) {
			// TODO: This is the frame() code!
			// wrap("test", std::bind(&FrameByFrameViewer::test, this));
			// wrap("test...", [this]() { OSG_NOTICE << "..." << std::endl; });

			frame(st);

			OpenThreads::Thread::microSleep(_sleep);

			st += static_cast<double>(_sleep) / 1000000.0;
		}

		return 0;
	}

private:
	osg::ref_ptr<RenderKeyHandler> _renderKeyHandler;

	bool _render = true;
	size_t _count = 0;

	// The value to sleep between each render frame check; same as the value that would
	// be passed to the Unix microsleep() function.
	unsigned int _sleep;
};

class IndexedVisitor: public osg::NodeVisitor {
public:
	IndexedVisitor():
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {}

	virtual void itraverse(osg::Node& n) {
		_i++;

		traverse(n);

		_i--;
	}

protected:
	inline constexpr size_t _index() const {
		return _i;
	}

private:
	size_t _i = 0;
};

class NameVisitor: public osg::NodeVisitor {
public:
	enum class NameMethod {
		CLASS
	};

	NameVisitor(NameMethod nameMethod=NameMethod::CLASS):
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
	_nameMethod(nameMethod) {
	}

	virtual void apply(osg::Node& n) {
		if(!n.getName().size()) {
			std::ostringstream name;

			if(_nameMethod == NameMethod::CLASS) {
				name << n.className() << "_" << _i;
			}

			n.setName(name.str());

			_i++;
		}

		traverse(n);
	}

private:
	NameMethod _nameMethod;

	unsigned int _i = 0;
};

class SceneStateVisitor: public IndexedVisitor {
public:
	SceneStateVisitor(const std::string& indent="   ", const std::string& separator="/"):
	_indent(indent),
	_separator(separator) {
	}

	virtual void apply(osg::Node& n) {
		_path.push_back(&n.getName());

		std::cout << _getIndent() << _getNameLibraryClass(n) << std::endl;

		itraverse(n);

		_path.pop_back();
	}

	virtual void apply(osg::Drawable& d) {
		_path.push_back(&d.getName());

		std::cout << _getIndent() << _getNameLibraryClass(d) << std::endl;

		_path.pop_back();
	}

protected:
	std::string _getIndent() const {
		std::ostringstream oss;

		for(auto i = 0; i < _index(); i++) oss << _indent;

		return oss.str();
	}

	template<typename T>
	std::string _getNameLibraryClass(const T& t) const {
		std::ostringstream oss;

		oss
			<< t.getName()
			// << _getPath()
			<< " (" << t.libraryName()
			<< "::" << t.className()
			<< ")"
		;

		return oss.str();
	}

	std::string _getPath() const {
		std::ostringstream oss;

		if(_path.size()) {
			for(auto i : _path) oss << _separator << *i;
		}

		return oss.str();
	}

private:
	std::vector<const std::string*> _path;

	std::string _indent;
	std::string _separator;

#if 0
	virtual void apply(osg::Geode& geode) {
		geode.setUpdateCallback(new UpdateCallback());

		/* for(unsigned int i=0;i<geode.getNumDrawables();++i) {
			geode.getDrawable(i)->setUpdateCallback(new DrawableUpdateCallback());
			geode.getDrawable(i)->setCullCallback(new DrawableCullCallback());
			geode.getDrawable(i)->setDrawCallback(new DrawableDrawCallback());
		} */
	}

	virtual void apply(osg::Transform& node) {
		apply(static_cast<osg::Node&>(node));
	}
#endif
};

}
