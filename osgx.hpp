#pragma once

#define OSGX_DISABLE_WARNINGS \
	_Pragma("GCC diagnostic push") \
	_Pragma("GCC diagnostic ignored \"-Wconversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"") \
	_Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wsign-compare\"") \
	_Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"") \
	_Pragma("GCC diagnostic ignored \"-Wshadow\"") \
	_Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"")

#define OSGX_ENABLE_WARNINGS \
	_Pragma("GCC diagnostic pop")

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

#include <optional>
#include <numeric>
#include <regex>
#include <concepts>
#include <ranges>
#include <span>

// TODO: Colored/custom OSG_NOTIFY (output as JSON)
// TODO: Name Visitor
// TODO: osgDebug; set "indent" based on pushed group
// TODO: A generic VisitorKeyHandler<Visitor>(key) that will run ANY visitor on the scene
// TODO: A visitor/traversal that will determine what is or is NOT culled

namespace osgx {
// ------------------------------------------------------------------------------------------------

// TODO: Add an ascii::esc() function that wraps a string in a color.
// TODO: Add support for foreground/background/bold/etc.
namespace ascii {
	// const std::string ESC = "\033[..m";

	/* constexpr std::string esc(std::string_view val) {
		return std::string("\033[") + std::string(val) + "m";
	} */

	const std::string BLACK = "\033[30m";
	const std::string RED = "\033[31m";
	const std::string GREEN = "\033[32m";
	const std::string YELLOW = "\033[33m";
	const std::string BLUE = "\033[34m";
	const std::string MAGENTA = "\033[35m";
	const std::string CYAN = "\033[36m";
	const std::string WHITE = "\033[37m";

	// BACKGROUND: 4*
	// BRIGHT FG: 9*

	const std::string RESET = "\033[0m";
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

// Handles the standard case, where you really DO want to create an instance; for example:
//
//	auto g0 = osgx::make_ref<osg::Geode>();
//	auto g1 = osgx::make_ref<osg::Geode>(geometry);
//
// No matter the arguments--even if NONE are passed--you will end up with an INSTANCE.
template<typename T, typename... Args>
auto make_ref(const Args&... args) {
	return osg::ref_ptr<T>(new T(args...));
}

// This is a special version that allows you to create a typed `osg::ref_ptr`, but one that is
// immediately set to `nullptr`; the version above will ALWAYS create an instance, no matter what
// you pass; this version will allow you to call it like this:
//
//	auto g0 = osgx::make_ref<osg::Geode>(nullptr);
//
// After the above call, you'll have an empty `osg::ref_ptr`, ready to hold something later on.
template<typename T, typename... Args>
auto make_ref(std::nullptr_t, const Args&... args) {
	return osg::ref_ptr<T>();
}

// NAMING objects is so common, it's worth having a helper for it!
template<typename T, typename... Args>
auto make_nref(const std::string& name, const Args&... args) {
	auto ref = osg::ref_ptr<T>(new T(args...));

	ref->setName(name);

	return ref;
}

constexpr auto tick = [](){ return osg::Timer::instance()->tick(); };

template<typename Func, typename... Args>
auto call(Func&& func, Args&&... args) -> decltype(auto) {
	if constexpr(!std::is_void_v<decltype(func(args...))>) {
		auto start = tick();
		auto result = func(std::forward<Args>(args)...);

		return std::make_pair(std::optional<decltype(func(args...))>(result), tick() - start);
	}

	else {
		auto start = tick();

		func(std::forward<Args>(args)...);

		return std::make_pair(std::nullopt, tick() - start);
	}
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

// ------------------------------------------------------------------------------------------------
// TODO: Can this be used with VisitorEventHandler?
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

			if(_options & Options::CLASS) name << "$" << cn << "_" << _cidmap[cn];

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

// class DescribeSceneVisitor: public IndexedVisitor {
class DescribeSceneVisitor: public NameVisitor {
public:
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

		// TODO: Temporary!
		n.setUserValue("path", _path.str());

		itraverse(n);

		_path.pop_back();
	}

	virtual void apply(osg::Drawable& d) override {
		_setName(d);

		_path.push_back(d.getName());

		std::cout << _getIndent() << "+ " << _getNameLibraryClass(d) << std::endl;

		// TODO: Temporary!
		d.setUserValue("path", _path.str());

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
			// << ascii::CYAN << t.getName() << ascii::RESET
			<< t.getName()
			<< " (" << t.libraryName()
			<< "::" << t.className()
			// << ") [@" << &t << "]"
			<< ")"
		;

		// TODO: When should this be included?
		if(false) oss << " {" << _path.str() << "}";

		return oss.str();
	}

private:
	Path _path;

	std::string _indent;
	std::string _separator;
};

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
}

// Runs the passed-in Visitor on the scene when a specified key is pressed.
//
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

// #define osgx_callthis(eval) osgx::call([this]() { eval ; })

class FilterNotifyHandler: public osg::NotifyHandler {
public:
	template<typename... Args>
	FilterNotifyHandler(Args&&... args) {
		(addFilter(std::forward<Args>(args)), ...);

		// Define patterns to filter/exclude!
		_filter.emplace_back("^draw()");
		_filter.emplace_back("^cull()");
		_filter.emplace_back("^end draw()");
		_filter.emplace_back("^end cull()");
		_filter.emplace_back(R"(^BufferObject::releaseGLObjects\(0)");
		_filter.emplace_back(R"(^BufferData::releaseGLObjects\(0)");
	}

	void notify(osg::NotifySeverity severity, const char* message) override {
		std::string msg(message);

		for(const auto& f : _filter) {
			if(std::regex_search(msg, f)) return;
		}

		std::cerr << msg;
	}

	void addFilter(const std::string& patternStr) {
		try {
			_filter.emplace_back(patternStr);
		}

		catch(const std::regex_error& e) {
			std::cerr << "Invalid regex pattern: " << patternStr << " (" << e.what() << ")\n";
		}
	}

private:
	std::vector<std::regex> _filter;
};

template<typename T>
concept OSGArray = requires {
	typename T::ElementDataType;
	requires std::derived_from<T, osg::Array>;
};

template<OSGArray BaseArray>
class Array: public BaseArray {
public:
	// using BaseArray = BaseArray;
	using ElementDataType = typename BaseArray::ElementDataType;

	using BaseArray::size;
	using BaseArray::resize;
	using BaseArray::begin;
	using BaseArray::end;
	using BaseArray::assign;

	META_Object(osgx, Array)

	Array() = default;

	Array(const Array& arr, const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY):
	BaseArray(arr, co) {
	}

	Array(std::initializer_list<ElementDataType> init) {
		assign(init.begin(), init.end());
	}

	template<std::ranges::input_range R>
	explicit Array(R&& r) {
		resize(std::ranges::size(r));

		std::ranges::copy(r, begin());
	}

	template<typename... Args>
	requires(std::convertible_to<Args, ElementDataType> && ...)
	explicit Array(Args&&... args) {
		constexpr size_t N = sizeof...(Args);

		resize(N);

		// I really, REALLY love "code folding", even though I need LLM/AI help at times to get
		// the syntax exactly right.
		size_t i = 0;

		(( (*this)[i++] = ElementDataType(std::forward<Args>(args)) ), ...);
	}

	// osg::Object* cloneType() const override { return new Array(); }
	// osg::Object* clone(const osg::CopyOp&) const override { return new Array(*this); }

	// --- Full-span access ----------------------------------------------------
	auto span() { return std::span<ElementDataType>(&(*this)[0], size()); }
	auto span() const { return std::span<const ElementDataType>(&(*this)[0], size()); }

	// --- Partial-span access -------------------------------------------------
	auto span(size_t start, size_t count) {
		return std::span<ElementDataType>(&(*this)[start], count);
	}

	auto span(size_t start, size_t count) const {
		return std::span<const ElementDataType>(&(*this)[start], count);
	}

	// --- Range-based views ---------------------------------------------------
	auto view() { return std::ranges::subrange(begin(), end()); }
	auto view() const { return std::ranges::subrange(begin(), end()); }

	// --- Partial-range view --------------------------------------------------
	auto view(size_t start, size_t count) {
		return std::ranges::subrange(begin() + start, begin() + start + count);
	}

	auto view(size_t start, size_t count) const {
		return std::ranges::subrange(begin() + start, begin() + start + count);
	}

	template<typename... Args>
	// static osg::ref_ptr<Array> create(Args&&... args) {
	static auto create(Args&&... args) {
		return osg::ref_ptr<Array>(new Array(std::forward<Args>(args)...));
	}

	// static osg::ref_ptr<Array> create(std::initializer_list<ElementDataType> init) {
	static auto create(std::initializer_list<ElementDataType> init) {
		return osg::ref_ptr<Array>(new Array(init));
	}
};

using Vec2Array = Array<osg::Vec2Array>;
using Vec3Array = Array<osg::Vec3Array>;
using Vec4Array = Array<osg::Vec4Array>;
using FloatArray = Array<osg::FloatArray>;

}
