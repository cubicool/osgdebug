#pragma once

#if defined(__clang__)
	#define OSGX_DISABLE_WARNINGS \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Wconversion\"") \
		_Pragma("clang diagnostic ignored \"-Wdeprecated-copy\"") \
		_Pragma("clang diagnostic ignored \"-Wfloat-conversion\"") \
		_Pragma("clang diagnostic ignored \"-Wsign-compare\"") \
		_Pragma("clang diagnostic ignored \"-Woverloaded-virtual\"") \
		_Pragma("clang diagnostic ignored \"-Wshadow\"") \
		_Pragma("clang diagnostic ignored \"-Wunused-but-set-variable\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("clang diagnostic pop")

#elif defined(__GNUC__)
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

#else
	#define OSGX_DISABLE_WARNINGS
	#define OSGX_ENABLE_WARNINGS
#endif

OSGX_DISABLE_WARNINGS

// TODO: Trim these down to only the essential when this header settles.
#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/PrimitiveSet>

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/GLExtensions>

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
#include <unordered_map>

namespace osgx {
// ------------------------------------------------------------------------------------------------

struct ObjectPath: public std::list<std::string> {
	auto str() const {
		return std::accumulate(begin(), end(), std::string(), [](
			const auto& l,
			const auto& r
		) {
			return l + '.' + r;
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

// Returns simple `tick`-based timing for the wrapped callable; nothing fancy.
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
	_function(function) {}

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
	_options(options) {}

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
	_separator(separator) {}

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
	ObjectPath _path;

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
	_visitor(visitor) {}

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
	BaseArray(arr, co) {}

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
	BaseElements(osg::PrimitiveSet::TRIANGLES) {}

	explicit DrawElements(GLenum mode):
	BaseElements(mode) {}

	DrawElements(const DrawElements& rhs, const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY):
	BaseElements(rhs, co) {}

	DrawElements(GLenum mode, std::initializer_list<value_type> init):
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

		for(auto&& v : r) push_back(static_cast<value_type>(v));
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
		return osg::ref_ptr<DrawElements>(new DrawElements(
			osg::PrimitiveSet::TRIANGLES,
			std::forward<Args>(args)...)
		);
	}

	template<typename... Args>
	requires(sizeof...(Args) % 2 == 0)
	static auto lines(Args&&... args) {
		return osg::ref_ptr<DrawElements>(new DrawElements(
			osg::PrimitiveSet::LINES,
			std::forward<Args>(args)...)
		);
	}

	template<typename... Args>
	static auto strip(Args&&... args) {
		return osg::ref_ptr<DrawElements>(new DrawElements(
			osg::PrimitiveSet::TRIANGLE_STRIP,
			std::forward<Args>(args)...)
		);
	}

	template<typename... Args>
	static auto fan(Args&&... args) {
		return osg::ref_ptr<DrawElements>(new DrawElements(
			osg::PrimitiveSet::TRIANGLE_FAN,
			std::forward<Args>(args)...)
		);
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
		return osg::ref_ptr<DrawElements>(new DrawElements(std::forward<Args>(args)...));
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
