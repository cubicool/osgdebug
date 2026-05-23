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

// TODO: Trim these down to only the essential when this header settles.
#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/PrimitiveSet>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgViewer/View>
// #include <osgViewer/ViewerBase>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <limits>
#include <utility>

#include <optional>
#include <numeric>
#include <regex>
#include <concepts>
#include <ranges>
#include <span>
#include <atomic>

namespace osgx {
// ------------------------------------------------------------------------------------------------

// TODO: Support std::string&/* and string_view?
using path_t = std::list<std::string>;

template<char Separator='/'>
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
			return l + Separator + r;
		});
	}
};

using LinuxPath = Path<'/'>;
using WindowsPath = Path<'\\'>; // note the double-backslash escape
using DotPath = Path<'.'>; // bonus: useful for OSG node name hierarchies

#ifdef _WIN32
	using FilePath = WindowsPath;

#else
	using FilePath = LinuxPath;
#endif

// ------------------------------------------------------------------------------------------------
using vec_t = osg::Vec3::value_type;

namespace literals {
	// using namespace std::literals;

	inline constexpr vec_t operator""_v(long double value) {
		return static_cast<vec_t>(value);
	}

	inline constexpr vec_t operator""_v(unsigned long long value) {
		return static_cast<vec_t>(value);
	}

	inline constexpr std::size_t operator""_sz(unsigned long long value) { return value; }
	inline constexpr std::size_t operator""_z(unsigned long long value) { return value; }
}

using namespace literals;
// using namespace std::literals;

template<typename T>
concept OSGReferenced = std::derived_from<T, osg::Referenced>;

// Handles the standard case, where you really DO want to create an instance; for example:
//
//	auto g0 = osgx::make_ref<osg::Geode>();
//	auto g1 = osgx::make_ref<osg::Geode>(geometry);
//
// No matter the arguments--even if NONE are passed--you will end up with an INSTANCE.

template<OSGReferenced T, typename... Args>
auto make_ref(Args&&... args) {
	return osg::ref_ptr<T>(new T(std::forward<Args>(args)...));
}

// This is a special version that allows you to create a typed `osg::ref_ptr`, but one that is
// immediately set to `nullptr`; the version above will ALWAYS create an instance, no matter what
// you pass; this version will allow you to call it like this:
//
//	auto g0 = osgx::make_ref<osg::Geode>(nullptr);
//
// After the above call, you'll have an empty `osg::ref_ptr`, ready to hold something later on.
template<OSGReferenced T>
auto make_ref(std::nullptr_t) {
	return osg::ref_ptr<T>();
}

// NAMING objects is so common, it's worth having a helper for it!
template<OSGReferenced T, typename... Args>
auto make_nref(const std::string& name, Args&&... args) {
	auto ref = osg::ref_ptr<T>(new T(std::forward<Args>(args)...));

	ref->setName(name);

	return ref;
}

constexpr auto tick = [](){ return osg::Timer::instance()->tick(); };

// osgx::call() with the decltype(func(args...)) doubling is a bit fragile (the return type is
// evaluated twice). In C++20 you'd use std::invoke_result_t. Not broken, just dated.
/* template<typename Func, typename... Args>
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
} */
template<typename Func, typename... Args>
auto call(Func&& func, Args&&... args) -> decltype(auto) {
	using Result = std::invoke_result_t<Func, Args...>;

	if constexpr(!std::is_void_v<Result>) {
		auto start = tick();
		auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

		return std::make_pair(std::optional<Result>(std::move(result)), tick() - start);
	}

	else {
		auto start = tick();

		std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

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
		//
		// Logic is correct but _i being size_t means wrapping is technically UB if you somehow hit
		// max... though practically never a real concern.
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

// AI had this to say about this macro:
//
// The RING_BUFFER_T macro exposing protected members via using is a bit gnarly. A friend or just
// making ring_buffer's members protected-by-default would be cleaner.
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

	constexpr auto average(size_t count) const {
		count = std::min(count, size());

		if(!count) return T(0);

		T sum = T(0);

		for(size_t j = 0; j < count; j++) sum += _data[(_i - 1 - j) % N];

		return sum / static_cast<T>(count);
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
	DotPath _path;

	std::string _indent;
	std::string _separator;
};

namespace scene {
	// TODO: More arguments.
	inline auto sphereAt(const std::string& name, const osg::Vec3& pos, vec_t radius) {
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
	// requires std::derived_from<T, osg::MixinVector<typename T::ElementDataType>>;
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

	template<std::ranges::input_range R>
	void append_range(R&& r) {
		if constexpr(std::ranges::sized_range<R>) {
			auto _size = size();

			resize(_size + std::ranges::size(r));

			std::ranges::copy(r, begin() + static_cast<
				typename std::iterator_traits<decltype(begin())>::difference_type
			>(_size));
		}

		else {
			for(auto&& v : r) push_back(ElementDataType(v));
		}
	}

	void append_range(std::initializer_list<ElementDataType> il) {
		append_range(std::ranges::subrange(il.begin(), il.end()));
	}

	// TODO: In C++23, we could use `std::views::repeat`!
	// void append_n(const ElementDataType& value, size_t n) {
	// 	append_range(std::views::repeat(value, n));
	// }

	template<std::size_t N>
	constexpr void append_n(const ElementDataType& value) {
		append_range(std::views::iota(0_sz, N) | std::views::transform([&](auto) {
			return value;
		}));
	}

	// Runetime version of the above...
	void append_n(const ElementDataType& value, size_t n) {
		append_range(std::views::iota(0_sz, n) | std::views::transform([&](auto) {
			return value;
		}));
	}

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

// ================================================================================================
// Concepts
// ================================================================================================

template<typename T>
concept OSGDrawElements =
requires {
	typename T::value_type;

	requires std::derived_from<T, osg::DrawElements>;
};

// ================================================================================================
// Generic wrapper
// ================================================================================================

template<OSGDrawElements BaseElements>
class DrawElements: public BaseElements {
public:
	using value_type = typename BaseElements::value_type;
	using index_type = value_type;

	using BaseElements::size;
	using BaseElements::resize;
	using BaseElements::begin;
	using BaseElements::end;
	using BaseElements::assign;
	using BaseElements::push_back;
	using BaseElements::reserve;

	META_Object(osgx, DrawElements)

	// --------------------------------------------------------------------------------------------
	// Constructors
	// --------------------------------------------------------------------------------------------

	DrawElements():
	BaseElements(osg::PrimitiveSet::TRIANGLES) {
	}

	explicit DrawElements(GLenum mode):
	BaseElements(mode) {
	}

	DrawElements(const DrawElements& rhs,
		const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY):
	BaseElements(rhs, co) {
	}

	DrawElements(GLenum mode,
		std::initializer_list<value_type> init):
	BaseElements(mode) {
		assign(init.begin(), init.end());
	}

	template<std::ranges::input_range R>
	DrawElements(GLenum mode, R&& r):
	BaseElements(mode) {
		append_range(std::forward<R>(r));
	}

	template<typename... Args>
	requires(std::convertible_to<Args, value_type> && ...)
	DrawElements(GLenum mode, Args&&... args):
	BaseElements(mode) {
		append(std::forward<Args>(args)...);
	}

	// --------------------------------------------------------------------------------------------
	// Append helpers
	// --------------------------------------------------------------------------------------------

	template<typename... Args>
	requires(std::convertible_to<Args, value_type> && ...)
	void append(Args&&... args) {
		(push_back(static_cast<value_type>(args)), ...);
	}

	template<std::ranges::input_range R>
	void append_range(R&& r) {
		if constexpr(std::ranges::sized_range<R>) {
			reserve(size() + std::ranges::size(r));
		}

		for(auto&& v : r) {
			push_back(static_cast<value_type>(v));
		}
	}

	void append_range(std::initializer_list<value_type> il) {
		append_range(std::views::all(il));
	}

	// --------------------------------------------------------------------------------------------
	// Triangle helpers
	// --------------------------------------------------------------------------------------------

	template<typename... Args>
	requires(sizeof...(Args) % 3 == 0)
	static auto triangles(Args&&... args) {
		return osg::ref_ptr<DrawElements>(
			new DrawElements(
				osg::PrimitiveSet::TRIANGLES,
				std::forward<Args>(args)...));
	}

	template<typename... Args>
	requires(sizeof...(Args) % 2 == 0)
	static auto lines(Args&&... args) {
		return osg::ref_ptr<DrawElements>(
			new DrawElements(
				osg::PrimitiveSet::LINES,
				std::forward<Args>(args)...));
	}

	template<typename... Args>
	static auto strip(Args&&... args) {
		return osg::ref_ptr<DrawElements>(
			new DrawElements(
				osg::PrimitiveSet::TRIANGLE_STRIP,
				std::forward<Args>(args)...));
	}

	template<typename... Args>
	static auto fan(Args&&... args) {
		return osg::ref_ptr<DrawElements>(
			new DrawElements(
				osg::PrimitiveSet::TRIANGLE_FAN,
				std::forward<Args>(args)...));
	}

	// --------------------------------------------------------------------------------------------
	// Safe push helpers
	// --------------------------------------------------------------------------------------------

	void checked_push(unsigned int v) {
		if(v > std::numeric_limits<value_type>::max()) {
			throw std::out_of_range("DrawElements index overflow");
		}

		push_back(static_cast<value_type>(v));
	}

	// --------------------------------------------------------------------------------------------
	// Views
	// --------------------------------------------------------------------------------------------

	auto span() {
		return std::span<value_type>(&(*this)[0], size());
	}

	auto span() const {
		return std::span<const value_type>(&(*this)[0], size());
	}

	auto view() {
		return std::ranges::subrange(begin(), end());
	}

	auto view() const {
		return std::ranges::subrange(begin(), end());
	}

	// --------------------------------------------------------------------------------------------
	// Factories
	// --------------------------------------------------------------------------------------------

	template<typename... Args>
	static auto create(Args&&... args) {
		return osg::ref_ptr<DrawElements>(
			new DrawElements(std::forward<Args>(args)...));
	}
};

// ================================================================================================
// Aliases
// ================================================================================================

using DrawElementsUByte = DrawElements<osg::DrawElementsUByte>;
using DrawElementsUShort = DrawElements<osg::DrawElementsUShort>;
using DrawElementsUInt = DrawElements<osg::DrawElementsUInt>;

class LambdaKeyHandler: public osgGA::GUIEventHandler {
public:
	// TODO: OSG needs to make this less ghettofied, and EXPOSE THE TYPE (int) somehow!
	using Key = int;
	using Keys = std::vector<Key>;
	using Function = std::function<bool(
		const osgGA::GUIEventAdapter&,
		osgGA::GUIActionAdapter&,
		Key
	)>;

	template<typename Fn>
	LambdaKeyHandler(Key key, Fn fn): _keys{key}, _fn(_wrap(std::move(fn))) {}

	template<typename Fn>
	LambdaKeyHandler(std::initializer_list<Key> keys, Fn fn):
	_keys(keys), _fn(_wrap(std::move(fn))) {}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		// if(ea.getHandled()) return false;

		if(ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN) {
			auto key = ea.getKey();

			if(std::ranges::find(_keys, key) != _keys.end()) {
				OSG_NOTICE << "In LambdaKeyHandler(key=" << key << ")" << std::endl;

				return _fn(ea, aa, key);
			}
		}

		return false;
	}

private:
	Keys _keys;
	Function _fn;

	template<typename Fn>
	static Function _wrap(Fn fn) {
		static_assert(
			std::is_invocable_v<Fn, const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter&> ||
			std::is_invocable_v<Fn, const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter&, Key>,
			"LambdaKeyHandler: function must accept (ea, aa) or (ea, aa, key)"
		);

		if constexpr(std::is_invocable_v<Fn,
			const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter&, Key>
		) {
			return fn;
		}

		else {
			return [fn = std::move(fn)](
				const osgGA::GUIEventAdapter& ea,
				osgGA::GUIActionAdapter& aa,
				Key
			) -> bool {
				return fn(ea, aa);
			};
		}
	}
};

class WriteTextureCallback: public osg::Camera::DrawCallback {
public:
	WriteTextureCallback(osg::Texture* t): _tex(t) {}

	virtual void operator()(osg::RenderInfo& ri) const {
		if(_write.exchange(false, std::memory_order_acq_rel)) {
			auto* state = ri.getState();

			_tex->apply(*state);

			auto img = make_ref<osg::Image>();

			img->readImageFromCurrentTexture(ri.getContextID(), false);

			osgDB::writeImageFile(*img, _name);

			OSG_NOTICE << "Writing: " << _name << std::endl;
		}
	}

	void write(const std::string& name) {
		_name = name;

		_write.store(true, std::memory_order_release);
	}

protected:
	osg::ref_ptr<osg::Texture> _tex;

	// NOTE: Using atomic flag with exchange() to safely trigger once from event thread.
	// I'm not going to lie, this bit was totally written by a LLM/AI. :) You MIGHT be able to
	// get away with JUST the `bool`, but it's bad form.
	mutable std::atomic<bool> _write{false};

	std::string _name;
};

template<typename Callback>
requires std::derived_from<Callback, osg::Referenced>
class CallbacksGroup: public Callback {
public:
	using Callbacks = std::vector<osg::ref_ptr<Callback>>;

	CallbacksGroup() = default;

	CallbacksGroup(std::initializer_list<Callback*> cbs) {
		for(auto* cb : cbs) add(cb);
	}

	void add(Callback* cb) {
		_callbacks.emplace_back(cb);
	}

	void remove(Callback* cb) {
		std::erase_if(_callbacks, [&](auto& p){ return p.get() == cb; });
	}

protected:
	Callbacks _callbacks;
};

class CameraDrawCallbacksGroup: public CallbacksGroup<osg::Camera::DrawCallback> {
public:
	using CallbacksGroup<osg::Camera::DrawCallback>::CallbacksGroup;

	void operator()(osg::RenderInfo& ri) const override {
		for(const auto& cb : _callbacks) (*cb)(ri);
	}
};

class NodeCallbacksGroup: public CallbacksGroup<osg::NodeCallback> {
public:
	using CallbacksGroup<osg::NodeCallback>::CallbacksGroup;

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		for(const auto& cb : _callbacks) (*cb)(node, nv);
	}
};

class DrawableDrawCallbacksGroup: public CallbacksGroup<osg::Drawable::DrawCallback> {
public:
	using CallbacksGroup<osg::Drawable::DrawCallback>::CallbacksGroup;

	void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* d) const override {
		for(const auto& cb : _callbacks) cb->drawImplementation(ri, d);
	}
};

template<typename Callback, typename Fn>
requires std::derived_from<Callback, osg::Referenced>
class LambdaCallbackBase: public Callback {
public:
	explicit LambdaCallbackBase(Fn fn): _fn(std::move(fn)) {}

protected:
	Fn _fn;
};

class CameraDrawLambdaCallback: public LambdaCallbackBase<
	osg::Camera::DrawCallback,
	std::function<void(osg::RenderInfo&)>
> {
public:
	using Base = LambdaCallbackBase<
		osg::Camera::DrawCallback,
		std::function<void(osg::RenderInfo&)>
	>;

	using Base::Base;

	void operator()(osg::RenderInfo& ri) const override {
		_fn(ri);
	}
};

class NodeLambdaCallback: public LambdaCallbackBase<
	osg::NodeCallback,
	std::function<void(osg::Node*, osg::NodeVisitor*)>
> {
public:
	using Base = LambdaCallbackBase<
		osg::NodeCallback,
		std::function<void(osg::Node*, osg::NodeVisitor*)>
	>;

	using Base::Base;

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		_fn(node, nv);
	}
};

}
