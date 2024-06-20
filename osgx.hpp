#pragma once

#define OSGX_DISABLE_WARNINGS \
	_Pragma("GCC diagnostic push") \
	_Pragma("GCC diagnostic ignored \"-Wconversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"") \
	_Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wsign-compare\"") \
	_Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"") \
	_Pragma("GCC diagnostic ignored \"-Wshadow\"")

#define OSGX_ENABLE_WARNINGS \
	_Pragma("GCC diagnostic pop")

// #include "chronode.hpp"

// CHRONODE_MILLITIMER(OSGX)

OSGX_DISABLE_WARNINGS

// TODO: Do I need these?
#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/Point>

#include <osgDB/Registry>
#include <osgDB/ReadFile>

#include <osgViewer/View>
// #include <osgViewer/ViewerBase>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <numeric>

// TODO: Colored/custom OSG_NOTIFY (output as JSON)
// TODO: Name Visitor
// TODO: osgDebug; set "indent" based on pushed group
// TODO: A generic VisitorKeyHandler<Visitor>(key) that will run ANY visitor on the scene
// TODO: A visitor/traversal that will determine what is or is NOT culled

namespace osgx {
// ------------------------------------------------------------------------------------------------

namespace ascii {
#if 0
Black: \033[30m
Red: \033[31m
Green: \033[32m
Yellow: \033[33m
Blue: \033[34m
Magenta: \033[35m
Cyan: \033[36m
White: \033[37m

BACKGROUND: 4*
BRIGHT FG: 9*
RESET: 0
#endif
}

// TODO: Support std::string&/*.
// TODO: Make separator configurable.
using path_t = std::list<std::string>;

struct Path: public path_t {
public:
	// using path_t::path_t;
	// using path_t::push_back;
	// using path_t::pop_back;
	// using path_t::begin;
	// using path_t::end;

	auto str() const {
		return std::accumulate(begin(), end(), std::string(), [](
			const auto& l,
			const auto& r
		) {
			return l + "/" + r;
		});
	}
};

// ------------------------------------------------------------------------------------------------
using vec_t = osg::Vec3::value_type;

namespace literals {
	// using namespace std::literals;

	inline constexpr vec_t operator""_v(long double value) {
		return static_cast<vec_t>(value);
	}

	inline constexpr std::size_t operator""_sz(unsigned long long value) { return value; }
	inline constexpr std::size_t operator""_z(unsigned long long value) { return value; }
}

using namespace literals;
// using namespace std::literals;

template<typename T, typename... Args>
auto make_ref(const Args&... args) {
	return osg::ref_ptr<T>(new T(args...));
}

template<typename T, typename... Args>
auto make_ref(std::nullptr_t, const Args&... args) {
	return osg::ref_ptr<T>();
}

template<typename T, typename... Args>
auto make_nref(const std::string& name, const Args&... args) {
	auto ref = osg::ref_ptr<T>(new T(args...));

	ref->setName(name);

	return ref;
}

// ------------------------------------------------------------------------------------------------
// Ring Buffer: manages a static-sized array (of size N), permitting new values
// by always replacing the oldest.
template<typename T, size_t N>
class ring_buffer {
public:
	using buffer_t = std::array<T, N>;

	// constexpr ring_buffer(): _data{T()} {}
	// constexpr ring_buffer(const T& d): _data{} {}
	// constexpr ring_buffer(std::initializer_list<T> d): _data(d) {}
	// constexpr ring_buffer(const buffer_t& d): _data(d) {}

	constexpr void add(const T& t) {
		_data[_i % N] = t;

		_i++;

		// If we roll over, make sure on the second iteration we begin at a value that won't
		// make the buffer look "underfull."
		if(_i == std::numeric_limits<size_t>::max()) _i = N;
	}

	constexpr const auto& data() const {
		return _data;
	}

	constexpr size_t size() const {
		return _i >= N ? N : _i;
	}

protected:
	buffer_t _data;

	size_t _i = 0;
};

#define RING_BUFFER_T(_T, _N) \
	using ring_buffer_t = ring_buffer<_T, _N>; \
	using ring_buffer_t::_i; \
	using ring_buffer_t::_data; \
	using ring_buffer_t::size;

// Arithmatic Ring Buffer: manages a static-sized array (of size N), permitting new values
// by always replacing the oldest. Provides an "average" method for determining the mean
// of all values (and skips indices that are not yet populated, if applicable).
// template<typename T, size_t N, typename = std::enable_if_t<std::is_arithmetic<T>::value>>
template<typename T, size_t N, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
class aring_buffer: public ring_buffer<T, N> {
public:
	RING_BUFFER_T(T, N)

	constexpr auto average() const {
		if(!_i) return T(0);

		// We use begin() + size() to make sure we only use valid, "add()'ed" values.
		// Otherwise, it might skew the average, factoring in a bunch of unwanted zeros.
		return std::accumulate(
			_data.begin(),
			_data.begin() + size(),
			T(0)
		) / static_cast<T>(size());
	}
};

#if 0
// ------------------------------------------------------------------------------------------------
enum class Option {
	Tesselation,
	PointSize
};

// Box, Cone, Cylinder, Capsule
template<typename S=osg::Sphere>
class Shape: public osg::ShapeDrawable {
public:
	using shape_t = S;

	Shape(vec_t radius=1.0) {
		_build(radius);
	}

	template<typename Opt, typename T, typename... Opts>
	Shape(vec_t radius, Opt opt, const T& val, Opts... opts) {
		_processOption(opt, val);

		if constexpr(sizeof...(Opts) > 1) {
			static_assert(sizeof...(Opts) % 2 == 0);

			_processOptions(opts...);
		}

		_build(radius);
	}

	template<typename... Args>
	static auto make_ref(const Args&... args) {
		return make_ref<Shape>(args...);
	}

protected:
	void _build(vec_t radius) {
		setShape(new S(osg::Vec3(0.0, 0.0, 0.0), radius));
	}

	template<typename Opt, typename T>
	void _processOption(Opt opt, const T& val) {
		auto o = static_cast<Option>(opt);

		if(o == Option::Tesselation) {
			auto* hints = new osg::TessellationHints();

			hints->setDetailRatio(val);

			setTessellationHints(hints);
		}

		else {
			getOrCreateStateSet()->setAttribute(
				new osg::Point(val),
				osg::StateAttribute::ON
			);
		}
	}
};

// ------------------------------------------------------------------------------------------------
class NodeCallback: public osg::NodeCallback {
public:
	NodeCallback(const std::string& msg): _msg(msg) {}

	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		std::cout << _msg << ": update callback - pre traverse" << node << std::endl;

		traverse(node,nv);

		std::cout << _msg << ": update callback - post traverse" << node << std::endl;
	}

private:
	std::string _msg;
};

class DrawableDrawCallback: public osg::Drawable::DrawCallback {
public:
	virtual void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const {
		// auto ext = renderInfo.getState()->get<osg::GLExtensions>();

		std::cout << ">> " << drawable->getName() << " draw call back - pre drawImplementation" << drawable << std::endl;

		// CHRONODE_START(OSGX, drawable->getName())

		// drawable->drawImplementation(renderInfo);

		// CHRONODE_STOP(OSGX)

		std::cout << ">> " << drawable->getName() << " draw call back - post drawImplementation" << drawable << std::endl;

		// const_cast<osg::Drawable*>(drawable)->dirtyGLObjects();
	}
};

class DrawableCullCallback: public osg::DrawableCullCallback {
public:
	virtual bool cull(osg::NodeVisitor*, osg::Drawable* drawable, osg::State* /*state*/) const {
		std::cout << "Drawable cull callback " << drawable << std::endl;

		return false;
	}
};

class DrawableUpdateCallback: public osg::DrawableUpdateCallback {
public:
	virtual void update(osg::NodeVisitor*, osg::Drawable* drawable) {
		std::cout << "Drawable update callback " << drawable << std::endl;
		/* drawable->getOrCreateStateSet()->addUniform(
			new osg::Uniform( "col", osg::Vec3(0.0f, 0.0f, 0.0f)
		));
		drawable->dirtyGLObjects(); */
	}
};

class ReadFileCallback: public osgDB::Registry::ReadFileCallback {
public:
	virtual osgDB::ReaderWriter::ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) {
		std::cout << "before readNode" << std::endl;

		// note when calling the Registry to do the read you have to call readNodeImplementation NOT readNode, as this will
		// cause on infinite recusive loop.
		osgDB::ReaderWriter::ReadResult result = osgDB::Registry::instance()->readNodeImplementation(fileName,options);

		std::cout << "after readNode" << std::endl;

		return result;
	}
};

class InsertCallbacksVisitor: public osg::NodeVisitor {
public:
	InsertCallbacksVisitor(): osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {}

	virtual void apply(osg::Node& node) {
		_checkName(&node);

		// node.setUpdateCallback(new NodeCallback("UPDATE"));
		// node.setCullCallback(new NodeCallback("CULL"));

		traverse(node);
	}

	virtual void apply(osg::Geode& geode) {
		_checkName(&geode);

		// geode.setUpdateCallback(new NodeCallback("UPDATE"));

		// note, it makes no sense to attach a cull callback to the node
		// at there are no nodes to traverse below the geode, only
		// drawables, and as such the Cull node callbacks is ignored.
		// If you wish to control the culling of drawables
		// then use a drawable cullback...

		for(unsigned int i = 0; i < geode.getNumDrawables(); i++) {
			auto* d = geode.getDrawable(i);

			_checkName(d);

			std::cout << "Found: " << d->getName() << std::endl;

			d->setUpdateCallback(new DrawableUpdateCallback());
			// d->setCullCallback(new DrawableCullCallback());
			d->setDrawCallback(new DrawableDrawCallback());
		}

		traverse(geode);
	}

	/* virtual void apply(osg::Transform& node) {
		apply(static_cast<osg::Node&>(node));
	} */

protected:
	void _checkName(osg::Object* o) {
		const auto& name = o->getName();

		if(!name.size()) {
			auto n = std::string(o->className()) + std::to_string(_i);

			OSG_NOTICE << "Naming: " << n << std::endl;

			o->setName(n);

			_i++;
		}
	}

	size_t _i = 0;
};
#endif

// ------------------------------------------------------------------------------------------------
template<typename Node=osg::Node>
class LambdaVisitor: public osg::NodeVisitor {
public:
	using Function = std::function<void(Node&)>;

	LambdaVisitor(Function function):
	osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
	_function(function) {
	}

	virtual void apply(Node& n) override {
		_function(n);

		traverse(n);
	}

	virtual void operator()(osg::Node* node) {
		node->accept(*this);
	}

protected:
	Function _function;
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
	inline size_t _index() const {
		return _i;
	}

private:
	size_t _i = 0;
};

// TODO: Call setUserValue("path", ...), if requested.
// class NameVisitor: public osg::NodeVisitor {
class NameVisitor: public IndexedVisitor {
public:
	// TODO: Add a "format" method, where the user can specify: "%c%i".
	// TODO: Considering adding a "%%" format, which uses a lambda for formatting.
	enum Options {
		CLASS = 1u,
		PATH = 1u << 1,
		FORCE = 1u << 2
	};

	NameVisitor(unsigned int options=Options::CLASS):
	// osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
	_options(options) {
	}

	// TODO: See below.
	virtual void apply(osg::Node& n) override {
		_setName(n);

		itraverse(n);
	}

protected:
	// TODO: Is this REALLY the best way to handle this? If a subclass wants to kick off the apply()
	// behavior FIRST (rather than before), I can't think of anything else...
	void _setName(osg::Node& n) {
		if(!n.getName().size()) {
			std::ostringstream name;

			const auto& cn = n.className();

			if(_options & Options::CLASS) name << cn << "_" << _cidmap[cn];

			n.setName(name.str());

			_cidmap[cn]++;
		}
	}

private:
	unsigned int _options;

	using ClassIDMap = std::map<std::string, size_t>;

	ClassIDMap _cidmap;
};

/*
│ - U+2500 (vertical bar)
─ - U+2500 (horizontal bar) can also be U+2550 (horizontal bar medium)
└ - U+2514
┌ - U+250C
┘ - U+2518
├ - U+251C
┬ - U+252C
std::cout << '\u2500' << std::endl;
*/

// TODO: Add terminal color output! :)
// class DescribeSceneVisitor: public IndexedVisitor {
class DescribeSceneVisitor: public NameVisitor {
public:
	// TODO: Create a osgViewer::EventHander for running this on a key press!
	// TODO: Make the common/tedious viewer retrieval cleaner/reusable!
	/* bool WindowSizeHandler::handle(const osgGA::GUIEventAdapter &ea, osgGA::GUIActionAdapter &aa) {
		osgViewer::View* view = dynamic_cast<osgViewer::View*>(&aa);
		if (!view) return false;

		osgViewer::ViewerBase* viewer = view->getViewerBase();

		if (viewer == NULL)
		{
			return false;
		}

		if (ea.getHandled()) return false;
	} */

	/* class EventHandler: public osgGA::GUIEventHandler {
	public:
		virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
			FrameByFrameViewer* viewer = dynamic_cast<FrameByFrameViewer*>(&aa);

			if(viewer && ea.getEventType() == osgGA::GUIEventAdapter::KEYUP) {
				if(ea.getKey() == 'n') {
					viewer->_render = true;

					return true;
				}
			}

			return false;
		}
	}; */

	DescribeSceneVisitor(const std::string& indent="   ", const std::string& separator="/"):
	_indent(indent),
	_separator(separator) {
	}

	virtual void apply(osg::Node& n) override {
		_setName(n);

		_path.push_back(n.getName());

		std::cout << _getIndent() << _getNameLibraryClass(n) << std::endl;

		/* std::cout << _getIndent() << _getNameLibraryClass(n);

		if(n.getNumParents() >= 1) {
			std::cout << " [";

			for(auto p = 0; p < n.getNumParents(); p++) {
				std::cout << "parent" << p << "=" << n.getParent(p)->getName() << " ";
			}

			std::cout << "]";
		}

		std::cout << std::endl; */

		itraverse(n);

		_path.pop_back();
	}

	virtual void apply(osg::Drawable& d) override {
		_setName(d);

		_path.push_back(d.getName());

		std::cout << _getIndent() << "+ " << _getNameLibraryClass(d) << std::endl;

		itraverse(d);

		_path.pop_back();
	}

protected:
	std::string _getIndent() const {
		std::ostringstream oss;

		/* for(auto i = 0_z; i < _index(); i++) {
			if(i + 1 == _index()) oss << _indent << "\u2514 ";

			else oss << _indent << "\u251c";
		} */

		for(auto i = 0_z; i < _index(); i++) oss << _indent;

		return oss.str();
	}

	template<typename T>
	std::string _getNameLibraryClass(const T& t) const {
		std::ostringstream oss;

		oss
			<< t.getName()
			<< " (" << t.libraryName()
			<< "::" << t.className()
			<< ") [" << _path.str()
			<< "]"
		;

		return oss.str();
	}

private:
	Path _path;

	std::string _indent;
	std::string _separator;
};

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

using GridCallback = std::function<void(size_t row, size_t col, const osg::Vec3& pos)>;

// TODO: Add different orientations to account for different view coordinate systems.
enum class GridSettings {
	ROWZ_COLX,
	ROWZNEG_COLX
};

void grid(
	size_t rows,
	size_t cols,
	GridCallback cb,
	GridSettings gs=GridSettings::ROWZNEG_COLX
) {
	for(decltype(rows) row = 0; row < rows; row++) {
		for(decltype(cols) col = 0; col < cols; col++) {
			if(gs == GridSettings::ROWZ_COLX) cb(row, col, osg::Vec3(
				static_cast<vec_t>(col),
				0.0_v,
				static_cast<vec_t>(row)
			));

			else cb(row, col, osg::Vec3(
				static_cast<vec_t>(col),
				0.0_v,
				-static_cast<vec_t>(row)
			));
		}
	}
}

namespace scene {
	// TODO: More arguments.
	auto sphereAt(const std::string& name, const osg::Vec3& pos, vec_t radius) {
		auto m = new osg::MatrixTransform(osg::Matrix::translate(pos));
		auto g = new osg::Geode();
		auto t = new osg::TessellationHints();

		t->setDetailRatio(0.25);

		auto s = new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0, 0.0, 0.0), radius), t);

		s->setName(name + ".spr");

		g->setName(name + ".geo");
		g->addDrawable(s);
		// g->setCullCallback(new DrawableCullCallback());

		m->setName(name);
		m->addChild(g);
		// m->setCullCallback(new CullCallback());

		return m;
	}

	// TODO: More arguments.
	auto sphereGrid(size_t rows, size_t cols) {
		auto g = make_ref<osg::Group>();

		grid(rows, cols, [&g](size_t row, size_t col, const osg::Vec3& pos) {
			std::cout << row << "x" << col << " = " << (pos * 10.0) << std::endl;

			std::ostringstream oss;

			oss << "Sphere_" << row << "x" << col;

			g->addChild(sphereAt(oss.str(), pos * 10.0, 2.5));
		});

		return g;
	}
}

// TODO: More control over how and with what the visitor is called.
// TODO: Figure out a better way than just hard-coding the osgViewer::Viewer assumption.
template<typename Visitor, typename Viewer=osgViewer::Viewer>
class VisitorEventHandler: public osgGA::GUIEventHandler {
public:
	// TODO: OSG needs to make this less ghettofied.
	using key_t = int;

	VisitorEventHandler(key_t key, Visitor* visitor=nullptr):
	_key(key),
	_visitor(visitor) {
	}

	virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		// TODO: Is this really necessary?
		if(ea.getHandled()) return false;

		/* auto* view = dynamic_cast<osgViewer::View*>(&aa);

		if(!view) return false;

		auto* viewer = view->getViewerBase(); */

		// auto* viewer = dynamic_cast<osgViewer::Viewer*>(&aa);
		auto* viewer = dynamic_cast<Viewer*>(&aa);

		if(!viewer) return false;

		if(
			ea.getEventType() == osgGA::GUIEventAdapter::KEYUP &&
			ea.getKey() == _key
		) {
			auto* root = viewer->getSceneData();

			if(_visitor) root->accept(*_visitor);

			else {
				auto v = Visitor();

				root->accept(v);
			}
		}

		return false;
	}

protected:
	key_t _key;

	osg::ref_ptr<Visitor> _visitor;
};

}
