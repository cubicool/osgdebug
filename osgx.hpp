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
		_Pragma("clang diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("clang diagnostic ignored \"-Wextra\"")

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
		_Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("GCC diagnostic ignored \"-Wextra\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("GCC diagnostic pop")

#else
	#define OSGX_DISABLE_WARNINGS
	#define OSGX_ENABLE_WARNINGS
#endif

// The same as `META_Object` but updated for modern C++ warnings.
#define OSGX_META_Object(library,name) \
	osg::Object* cloneType() const override { return new name (); } \
	osg::Object* clone(const osg::CopyOp& copyop) const override { return new name (*this,copyop); } \
	bool isSameKindAs(const osg::Object* obj) const override { return dynamic_cast<const name *>(obj)!=NULL; } \
	const char* libraryName() const override { return #library; }\
	const char* className() const override { return #name; }

OSGX_DISABLE_WARNINGS

// TODO: Trim these down to only the essential when this header settles.
#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/ShapeDrawable>
#include <osg/PrimitiveSet>

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/GLExtensions>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgGA/CameraManipulator>

#include <osgViewer/View>
// #include <osgViewer/ViewerBase>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <numeric>
#include <regex>
#include <concepts>
#include <ranges>
#include <span>
#include <atomic>
#include <stdexcept>
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

// Returns the "first parent of type `T`" that can be found, and is a little "less heavy" than other
// options in OSG (`getParentalNodeList()`). Just fire-and-forget.
template<typename T, typename Node>
T* getFirstParent(const Node* node) {
	for(unsigned int i = 0; i < node->getNumParents(); i++) {
		const auto* p = node->getParent(i);

		if(const auto* t = dynamic_cast<const T*>(p)) return const_cast<T*>(t);

		if(auto* t = getFirstParent<T>(p)) return t;
	}

	return nullptr;
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

	OSGX_META_Object(osgx, Array)

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

	OSGX_META_Object(osgx, DrawElements)

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
				// OSG_NOTICE << "In LambdaKeyHandler(key=" << key << ")" << std::endl;

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
// TODO: This is only PBO-delayed readback, not strictly non-stalling async: the next-frame
// glMapBuffer() can still block if the transfer has not completed. Fix by adding a fence
// (glFenceSync + zero-timeout glClientWaitSync polling) or a deeper PBO ring before mapping.
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

// ================================================================================================
// MultiCameraManipulator
//
// Composite camera manipulator that routes input to one active manipulator while letting targets
// drive either the viewer's main camera or a dedicated camera such as an RTT camera.
// ================================================================================================
class MultiCameraManipulator: public osgGA::CameraManipulator {
public:
	struct Target {
		std::string name;
		osg::ref_ptr<osgGA::CameraManipulator> manipulator;
		osg::observer_ptr<osg::Camera> camera;
		osg::observer_ptr<osg::Node> scene;
		std::function<void(bool)> setActive;
	};

	void setToggleKey(int key) { _toggleKey = key; }
	int getToggleKey() const { return _toggleKey; }

	void addTarget(
		const std::string& name,
		osgGA::CameraManipulator* manipulator,
		osg::Camera* camera=nullptr,
		osg::Node* scene=nullptr,
		std::function<void(bool)> setActive={}
	) {
		Target target;
		target.name = name;
		target.manipulator = manipulator;
		target.camera = camera;
		target.scene = scene;
		target.setActive = setActive;

		if(target.manipulator.valid()) {
			target.manipulator->setNode(scene ? scene : _defaultScene.get());
		}

		_targets.push_back(target);

		if(_targets.size() == 1) activate(0);
	}

	unsigned int getActiveIndex() const { return _active; }
	unsigned int getNumTargets() const { return static_cast<unsigned int>(_targets.size()); }

	Target* activeTarget() {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	const Target* activeTarget() const {
		return _active < _targets.size() ? &_targets[_active] : nullptr;
	}

	void activate(unsigned int index) {
		if(index >= _targets.size()) return;

		if(_hasActive && _active == index) return;

		if(_hasActive && _active < _targets.size() && _targets[_active].setActive) {
			_targets[_active].setActive(false);
		}

		_active = index;
		_hasActive = true;

		auto& target = _targets[_active];

		if(target.manipulator.valid()) {
			target.manipulator->finishAnimation();
			target.manipulator->setNode(target.scene.valid() ? target.scene.get() : _defaultScene.get());

			if(target.camera.valid()) target.manipulator->setByInverseMatrix(target.camera->getViewMatrix());
		}

		if(target.setActive) target.setActive(true);

		OSG_NOTICE << "MultiCameraManipulator: " << target.name << std::endl;
	}

	void next() {
		if(!_targets.empty()) activate((_active + 1u) % static_cast<unsigned int>(_targets.size()));
	}

	void setByMatrix(const osg::Matrixd& matrix) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->setByMatrix(matrix);
		}
	}

	void setByInverseMatrix(const osg::Matrixd& matrix) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->setByInverseMatrix(matrix);
		}
	}

	osg::Matrixd getMatrix() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getMatrix() : osg::Matrixd();
	}

	osg::Matrixd getInverseMatrix() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getInverseMatrix() : osg::Matrixd();
	}

	void updateCamera(osg::Camera& mainCamera) override {
		auto* target = activeTarget();

		if(!target || !target->manipulator.valid()) return;

		setupMainCamera(mainCamera);

		if(target->camera.valid()) target->manipulator->updateCamera(*target->camera);
		else target->manipulator->updateCamera(mainCamera);
	}

	void setNode(osg::Node* node) override {
		_defaultScene = node;

		for(auto& target : _targets) {
			if(target.manipulator.valid()) {
				target.manipulator->setNode(target.scene.valid() ? target.scene.get() : _defaultScene.get());
			}
		}
	}

	osg::Node* getNode() override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
	}

	const osg::Node* getNode() const override {
		auto* target = activeTarget();

		return target && target->manipulator.valid() ? target->manipulator->getNode() : nullptr;
	}

	void home(double currentTime) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->home(currentTime);
		}
	}

	void home(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->home(ea, aa);
		}
	}

	void init(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(auto* target = activeTarget(); target && target->manipulator.valid()) {
			target->manipulator->init(ea, aa);
		}
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && ea.getKey() == _toggleKey) {
			if(auto* target = activeTarget(); target && target->manipulator.valid()) {
				target->manipulator->init(ea, aa);
			}

			next();

			if(auto* target = activeTarget(); target && target->manipulator.valid()) {
				target->manipulator->init(ea, aa);
			}

			return true;
		}

		auto* target = activeTarget();

		return target && target->manipulator.valid() && target->manipulator->handle(ea, aa);
	}

private:
	void setupMainCamera(osg::Camera& mainCamera) {
		if(_mainCameraSetup) return;

		_mainCameraSetup = true;

		for(auto& candidate : _targets) {
			if(!candidate.camera.valid() && candidate.manipulator.valid()) {
				candidate.manipulator->finishAnimation();
				candidate.manipulator->setNode(candidate.scene.valid() ? candidate.scene.get() : _defaultScene.get());
				candidate.manipulator->home(0.0);
				candidate.manipulator->updateCamera(mainCamera);

				break;
			}
		}
	}

	std::vector<Target> _targets;
	osg::observer_ptr<osg::Node> _defaultScene;
	unsigned int _active = 0;
	int _toggleKey = 'x';
	bool _hasActive = false;
	bool _mainCameraSetup = false;
};

// ================================================================================================
// Ortho2DManipulator
//
// Pan/zoom camera manipulator for orthographic 2D scenes.
//
// Controls:
//
// Left drag pan in world XY
// Scroll geometric zoom (_wheelZoomFactor per click)
// Shift+Scroll pixel-nudge zoom (_pixelNudge screen pixels per click)
// Ctrl+Left drag 3D pitch/yaw (unlocks rotation around center)
// Space / Home reset to home
//
// The manipulator owns the projection matrix: updateCamera() sets both view and
// projection each frame, so callers do NOT need to configure the camera projection
// separately.
//
// TODO: consider delegating to or toggling a full OrbitManipulator for persistent
// 3D navigation (currently Ctrl+drag accumulates rotation, release keeps it).
// ================================================================================================
class Ortho2DManipulator: public osgGA::CameraManipulator {
public:
	OSGX_META_Object(osgx, Ortho2DManipulator)

	Ortho2DManipulator() = default;

	OSGX_DISABLE_WARNINGS

		Ortho2DManipulator(
			const Ortho2DManipulator& m,
			const osg::CopyOp& co=osg::CopyOp::SHALLOW_COPY
		):
		osgGA::CameraManipulator(m, co),
		_center(m._center),
		_halfExtentY(m._halfExtentY),
		_minHalfExtent(m._minHalfExtent),
		_maxHalfExtent(m._maxHalfExtent),
		_pixelNudge(m._pixelNudge),
		_wheelZoomFactor(m._wheelZoomFactor),
		_rotateSensitivity(m._rotateSensitivity),
		_rotation(m._rotation),
		_node(m._node) {}

	OSGX_ENABLE_WARNINGS

	// Config
	void setPixelNudge(double n) { _pixelNudge = n; }
	double getPixelNudge() const { return _pixelNudge; }

	void setWheelZoomFactor(double f) { _wheelZoomFactor = f; }
	double getWheelZoomFactor() const { return _wheelZoomFactor; }

	void setZoomLimits(double minH, double maxH) { _minHalfExtent = minH; _maxHalfExtent = maxH; }
	void setRotateSensitivity(double s) { _rotateSensitivity = s; }

	// State
	void setCenter(const osg::Vec3d& c) { _center = c; }
	const osg::Vec3d& getCenter() const { return _center; }

	void setHalfExtentY(double h) {
		_halfExtentY = std::clamp(h, _minHalfExtent, _maxHalfExtent);
	}

	double getHalfExtentY() const { return _halfExtentY; }

	// CameraManipulator interface
	void setNode(osg::Node* node) override { _node = node; }
	const osg::Node* getNode() const override { return _node.get(); }
	osg::Node* getNode() override { return _node.get(); }

	// Extract pan center from the translation component of the camera-to-world matrix.
	void setByMatrix(const osg::Matrixd& m) override {
		_center.set(m(3, 0), m(3, 1), m(3, 2));
	}

	void setByInverseMatrix(const osg::Matrixd& m) override {
		setByMatrix(osg::Matrixd::inverse(m));
	}

	// Camera-to-world: undo the view matrix composition.
	osg::Matrixd getMatrix() const override {
		return osg::Matrixd::inverse(getInverseMatrix());
	}

	// World-to-camera (view matrix).
	// Orbit convention: center to origin -> rotate (pivot is now at origin) -> pull back.
	// OSG uses row vectors, so A*B*C applies A first; pull-back must come last.
	osg::Matrixd getInverseMatrix() const override {
		return
			osg::Matrixd::translate(-_center) *
			osg::Matrixd::rotate(_rotation) *
			osg::Matrixd::translate(0.0, 0.0, -1.0)
		;
	}

	// Sets BOTH view and projection so the caller owns neither.
	//
	// We take ownership of near/far (DO_NOT_COMPUTE_NEAR_FAR) because OSG's bounding-volume
	// computation clamps near > 0 even for ortho, which clips geometry that lands at negative
	// depth when the camera is tilted in 3D. We derive tight near/far analytically from the
	// scene bounding sphere each frame instead.
	void updateCamera(osg::Camera& cam) override {
		cam.setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
		cam.setViewMatrix(getInverseMatrix());

		const auto* vp = cam.getViewport();
		double aspect = (vp && vp->height() > 0.0)
			? vp->width() / vp->height()
			: 1.0
		;

		double h = _halfExtentY;
		double nearPlane = -1e6;
		double farPlane = 1e6;

		if(_node.valid()) {
			osg::BoundingSphere bs = _node->getBound();

			if(bs.radius() > 0.0) {
				// Eye position and forward vector in world space, derived from the view matrix.
				// Orbit: eye = center + rotation.conj() * (0, 0, 1)
				osg::Vec3d eye = _center + _rotation.conj() * osg::Vec3d(0.0, 0.0, 1.0);
				osg::Vec3d fwd = _rotation.conj() * osg::Vec3d(0.0, 0.0, -1.0);

				// Signed depth of the scene center along the view axis.
				double depth = (osg::Vec3d(bs.center()) - eye) * fwd;
				double r = bs.radius() * 1.1; // 10% padding

				nearPlane = depth - r;
				farPlane = depth + r;
			}
		}

		cam.setProjectionMatrixAsOrtho(
			-h * aspect, h * aspect,
			-h, h,
			nearPlane, farPlane
		);
	}

	void home(const osgGA::GUIEventAdapter&, osgGA::GUIActionAdapter& aa) override {
		_rotation = osg::Quat();

		if(_node.valid()) {
			auto bs = _node->getBound();

			_center = osg::Vec3d(bs.center());
			_halfExtentY = (bs.radius() > 0.0) ? bs.radius() * 1.2 : 1.0;
		}

		else {
			_center.set(0.0, 0.0, 0.0);
			_halfExtentY = 1.0;
		}

		aa.requestRedraw();
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		switch(ea.getEventType()) {
		case osgGA::GUIEventAdapter::PUSH:
			_lastX = ea.getXnormalized();
			_lastY = ea.getYnormalized();
			_dragging = (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);

			return false;

		case osgGA::GUIEventAdapter::RELEASE:
			_dragging = false;

			return false;

		case osgGA::GUIEventAdapter::DRAG: {
			if(!_dragging) return false;

			double nx = ea.getXnormalized();
			double ny = ea.getYnormalized();
			double dx = nx - _lastX;
			double dy = ny - _lastY;

			_lastX = nx;
			_lastY = ny;

			bool ctrl = (ea.getModKeyMask() & osgGA::GUIEventAdapter::MODKEY_CTRL) != 0;

			if(ctrl) {
				// 3D: pitch/yaw orbit around center.
				// Yaw rotates around world Y; pitch rotates around the camera's current right axis.
				osg::Vec3d right = _rotation.conj() * osg::Vec3d(1.0, 0.0, 0.0);

				_rotation =
					osg::Quat(dy * _rotateSensitivity, right) *
					osg::Quat(-dx * _rotateSensitivity, osg::Vec3d(0.0, 1.0, 0.0)) *
					_rotation
				;
			}

			else {
				// Pan along camera right/up so speed is constant regardless of rotation.
				double aspect = ea.getWindowWidth() > 0
					? double(ea.getWindowWidth()) / double(ea.getWindowHeight())
					: 1.0
				;

				osg::Vec3d right = _rotation.conj() * osg::Vec3d(1.0, 0.0, 0.0);
				osg::Vec3d up = _rotation.conj() * osg::Vec3d(0.0, 1.0, 0.0);

				_center -= right * dx * _halfExtentY * aspect;
				_center -= up * dy * _halfExtentY;
			}

			aa.requestRedraw();

			return false;
		}

		case osgGA::GUIEventAdapter::SCROLL: {
			bool shift = (ea.getModKeyMask() & osgGA::GUIEventAdapter::MODKEY_SHIFT) != 0;
			bool up = (ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP);

			if(shift) {
				// Pixel-nudge: each click moves the visible boundary by exactly _pixelNudge pixels.
				int winH = ea.getWindowHeight();
				double worldPerPixel = 2.0 * _halfExtentY / double(winH > 0 ? winH : 1);

				_halfExtentY += (up ? -1.0 : 1.0) * _pixelNudge * worldPerPixel;
			}

			else _halfExtentY *= up ? (1.0 / _wheelZoomFactor) : _wheelZoomFactor;

			_halfExtentY = std::clamp(_halfExtentY, _minHalfExtent, _maxHalfExtent);

			aa.requestRedraw();

			return true;
		}

		case osgGA::GUIEventAdapter::KEYDOWN:
			if(
				ea.getKey() == osgGA::GUIEventAdapter::KEY_Space ||
				ea.getKey() == osgGA::GUIEventAdapter::KEY_Home
			) {
				home(ea, aa);
				return true;
			}

			return false;

		default:
			return false;
		}
	}

private:
	osg::Vec3d _center{0.0, 0.0, 0.0};

	double _halfExtentY{1.0};
	double _minHalfExtent{1e-4};
	double _maxHalfExtent{1e6};
	double _pixelNudge{1.0};
	double _wheelZoomFactor{1.15};
	double _rotateSensitivity{2.0};

	osg::Quat _rotation; // identity = pure top-down 2D
	osg::ref_ptr<osg::Node> _node;

	bool _dragging{false};
	double _lastX{0.0};
	double _lastY{0.0};
};

// ================================================================================================
// Grid
//
// Procedurally-generated, antialiased, view-aware grid lines on a single quad. It supports crisp
// constant screen-pixel lines for flat overlays and grid/world-unit line widths for perspective
// ground planes. Both modes are derivative-driven off a "grid space" coordinate (UV * canvasSize),
// so no MVP decomposition is needed; the screen-space derivative already bakes in model, view, AND
// projection.
//
// Typical usage:
//
//   // Fullscreen background grid, always drawn first (the osgSlug HUD/UI use case):
//   auto camera = osgx::Grid::createOrthoCamera();
//   viewer.setSceneData(camera);
//   viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT); // don't let the main cam stomp it
//
//   // A real 3D ground plane instead:
//   auto grid = osgx::make_ref<osgx::Grid>(
//       osg::Vec3(-10,0,-10), osg::Vec3(20,0,0), osg::Vec3(0,0,20)
//   );
//   auto geode = osgx::make_ref<osg::Geode>();
//   geode->addDrawable(grid);
//
//   // Live updates, either way:
//   grid->setGridInterval(1.0f);
//   grid->setEdgeMode(osgx::Grid::EDGE_NUDGE);
// ================================================================================================

// Vertex shader hands the fragment shader a position in "grid space": UV (0..1) scaled by the
// logical canvas size. That keeps the grid's notion of "5 units apart" tied to the canvas the
// caller specified, independent of how big the quad actually is in NDC/world.
inline constexpr const char* GRID_VERTEX_SHADER = R"GLSL(
#version 330 core

uniform mat4 osg_ModelViewProjectionMatrix;
uniform vec2 u_canvasSize;

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

out vec2 gridPos;

void main(void) {
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;

	gridPos = osg_MultiTexCoord0 * u_canvasSize;
}
)GLSL";

inline constexpr const char* GRID_FRAGMENT_SHADER = R"GLSL(
#version 330 core

in vec2 gridPos;

uniform vec2 u_canvasSize;

uniform float u_gridInterval;
uniform float u_gridIntervalStrong; // <= 0 disables the "extra strength" tier
uniform float u_lineWidthPx;
uniform float u_lineWidth;

uniform vec4 u_colorBg;
uniform vec4 u_colorLine;
uniform vec4 u_colorLineStrong;

// Lines that fall exactly on the canvas boundary (pos == 0 or pos == u_canvasSize) only have
// geometry on one side, so their AA kernel is half-clipped and they render at roughly half
// intensity/width -- u_edgeMode picks how to handle that:
//   0 EDGE_ASIS  -- leave the half-clipped line as rendered (matches raw geometry)
//   1 EDGE_HIDE  -- don't draw boundary lines at all (clean edge, good for screen-aligned UI)
//   2 EDGE_NUDGE -- shift boundary lines inward by half their width so they render at full
//                   strength (good for a true 3D ground plane, where there's no "UI edge")
const int EDGE_ASIS = 0;
const int EDGE_HIDE = 1;
const int EDGE_NUDGE = 2;

uniform int u_edgeMode;
uniform int u_lineMode;

out vec4 fragColor;

// Antialiased coverage (0..1) of gridlines spaced `interval` apart in `pos`'s units, with the
// line drawn `lineWidthPx` screen-pixels wide regardless of view distance/angle.
float gridLine(vec2 pos, float interval, float lineWidthPx) {
	vec2 coord = pos / interval;
	vec2 deriv = fwidth(coord);

	// Guards against NaN/Inf when the derivative collapses to zero (e.g. the quad edge-on
	// to the view, or an interval of 0 from a not-yet-initialized uniform).
	deriv = max(deriv, vec2(1e-6));

	// Which line (in units of `interval`) is nearest each axis, and is that line one of the
	// two canvas-boundary lines? Computed from the raw (pre-AA-bias, pre-nudge) coordinate so
	// it reflects true geometry, independent of the pixel-alignment tricks below.
	vec2 maxCoord = u_canvasSize / interval;
	vec2 nearest = floor(coord + 0.5);
	bvec2 isMinEdge = lessThan(abs(nearest), vec2(0.5));
	bvec2 isMaxEdge = lessThan(abs(nearest - maxCoord), vec2(0.5));

	// The Cairo/Direct2D "+0.5" hairline trick: a mathematical line that lands exactly on a
	// pixel BOUNDARY gets its coverage split 50/50 across the two neighboring pixels instead
	// of drawn crisply into one, roughly doubling its apparent width. Biasing by half a screen
	// pixel re-centers that case onto a single pixel; harmless elsewhere since it's a constant
	// sub-pixel nudge of the whole grid pattern. Default bias for every non-edge fragment.
	vec2 bias = deriv * 0.5;

	// For edge lines under EDGE_NUDGE, REPLACE (not add to) the resonance bias above with the
	// nudge-inward amount -- stacking both cancels to zero on min edges (line stays on the
	// boundary, still half-clipped) and doubles up on max edges (shifts a full pixel in), which
	// is exactly the bottom/left-vs-top/right asymmetry this branch is here to avoid.
	if(u_edgeMode == EDGE_NUDGE) {
		vec2 halfWidthCoord = deriv * max(lineWidthPx, 0.0) * 0.5;

		if(isMinEdge.x) bias.x = -halfWidthCoord.x;
		else if(isMaxEdge.x) bias.x = halfWidthCoord.x;

		if(isMinEdge.y) bias.y = -halfWidthCoord.y;
		else if(isMaxEdge.y) bias.y = halfWidthCoord.y;
	}

	coord += bias;

	vec2 dist = abs(fract(coord + 0.5) - 0.5); // distance to nearest line, in grid cells
	vec2 distPx = dist / deriv; // same distance, converted to actual screen pixels
	vec2 halfWidth = vec2(max(lineWidthPx, 0.0) * 0.5);
	vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + 1.0, distPx);

	if(u_edgeMode == EDGE_HIDE) {
		if(isMinEdge.x || isMaxEdge.x) line.x = 0.0;
		if(isMinEdge.y || isMaxEdge.y) line.y = 0.0;
	}

	return clamp(max(line.x, line.y), 0.0, 1.0);
}

// Adapted from Ben Golus' "Pristine Grid" technique:
// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// `lineWidth` is in the same units as `pos`/`interval`, so perspective naturally makes lines
// thinner; once they approach sub-pixel size, they are widened in derivative space and faded by
// coverage.
float pristineGridLine(vec2 pos, float interval, float lineWidth) {
	vec2 uv = pos / interval;
	vec2 uvDDX = dFdx(uv);
	vec2 uvDDY = dFdy(uv);
	vec2 uvDeriv = max(vec2(length(vec2(uvDDX.x, uvDDY.x)), length(vec2(uvDDX.y, uvDDY.y))), vec2(1e-6));

	vec2 lineWidthCell = clamp(vec2(lineWidth / interval), vec2(0.0), vec2(1.0));
	bvec2 invertLine = greaterThan(lineWidthCell, vec2(0.5));
	vec2 targetWidth = mix(lineWidthCell, vec2(1.0) - lineWidthCell, invertLine);
	vec2 drawWidth = clamp(targetWidth, uvDeriv, vec2(0.5));
	vec2 lineAA = uvDeriv * 1.5;
	vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);

	gridUV = mix(vec2(1.0) - gridUV, gridUV, invertLine);

	vec2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);

	grid2 *= clamp(targetWidth / drawWidth, vec2(0.0), vec2(1.0));
	grid2 = mix(grid2, targetWidth, clamp(uvDeriv * 2.0 - 1.0, vec2(0.0), vec2(1.0)));
	grid2 = mix(grid2, vec2(1.0) - grid2, invertLine);

	if(u_edgeMode == EDGE_HIDE) {
		vec2 maxCoord = u_canvasSize / interval;
		vec2 nearest = floor(uv + 0.5);
		bvec2 isMinEdge = lessThan(abs(nearest), vec2(0.5));
		bvec2 isMaxEdge = lessThan(abs(nearest - maxCoord), vec2(0.5));

		if(isMinEdge.x || isMaxEdge.x) grid2.x = 0.0;
		if(isMinEdge.y || isMaxEdge.y) grid2.y = 0.0;
	}

	return clamp(grid2.x * (1.0 - grid2.y) + grid2.y, 0.0, 1.0);
}

void main(void) {
	vec4 color = u_colorBg;

	float thin = u_lineMode == 0 ?
		gridLine(gridPos, u_gridInterval, u_lineWidthPx) :
		pristineGridLine(gridPos, u_gridInterval, u_lineWidth);

	color = mix(color, u_colorLine, thin);

	if(u_gridIntervalStrong > 0.0) {
		float strong = u_lineMode == 0 ?
			gridLine(gridPos, u_gridIntervalStrong, u_lineWidthPx * 1.5) :
			pristineGridLine(gridPos, u_gridIntervalStrong, u_lineWidth * 1.5);

		color = mix(color, u_colorLineStrong, strong);
	}

	fragColor = color;
}
)GLSL";

class Grid: public osg::Geometry {
public:
	OSGX_META_Object(osgx, Grid)

	// Boundary-line handling for lines that fall exactly on the canvas edge (pos == 0 or
	// pos == canvasSize) -- see EDGE_ASIS/EDGE_HIDE/EDGE_NUDGE in GRID_FRAGMENT_SHADER above
	// for the full rationale.
	enum EdgeMode {
		EDGE_ASIS = 0,  // leave half-clipped boundary lines as rendered
		EDGE_HIDE = 1,  // don't draw boundary lines at all
		EDGE_NUDGE = 2  // shift boundary lines inward by half their width to full strength
	};

	enum LineMode {
		LINE_SCREEN_PIXELS = 0,  // constant screen-pixel hairlines, useful for HUD/ortho grids
		LINE_GRID_UNITS = 1      // perspective/world-width lines with derivative-limited AA
	};

	// Default: a fullscreen NDC quad (XY plane, z=0, -1..1) -- the most common case, meant to
	// pair with orthoCamera()/createOrthoCamera() below.
	Grid() {
		_build(osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0));
	}

	// `corner`/`widthVec`/`heightVec` place the quad in whatever space it ends up added to the
	// scene graph in -- NDC for a fullscreen overlay, or world-space for a real 3D ground plane.
	Grid(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec) {
		_build(corner, widthVec, heightVec);
	}

	Grid(const Grid& g, const osg::CopyOp& co = osg::CopyOp::SHALLOW_COPY):
	osg::Geometry(g, co) {
		_bindUniforms();
	}

	// --- Live uniform updates ---------------------------------------------------------------

	void setCanvasSize(const osg::Vec2& v) { _canvasSize->set(v); }
	void setGridInterval(float v) { _gridInterval->set(v); }
	void setGridIntervalStrong(float v) { _gridIntervalStrong->set(v); } // <= 0 disables
	void setLineWidthPx(float v) { _lineWidthPx->set(v); }
	void setLineWidth(float v) { _lineWidth->set(v); }
	void setEdgeMode(EdgeMode v) { _edgeMode->set(static_cast<int>(v)); }
	void setLineMode(LineMode v) { _lineMode->set(static_cast<int>(v)); }
	void setColorBg(const osg::Vec4& v) { _colorBg->set(v); }
	void setColorLine(const osg::Vec4& v) { _colorLine->set(v); }
	void setColorLineStrong(const osg::Vec4& v) { _colorLineStrong->set(v); }

	osg::Vec2 getCanvasSize() const { osg::Vec2 v; _canvasSize->get(v); return v; }
	float getGridInterval() const { float v = 0.0f; _gridInterval->get(v); return v; }
	float getGridIntervalStrong() const { float v = 0.0f; _gridIntervalStrong->get(v); return v; }
	float getLineWidthPx() const { float v = 0.0f; _lineWidthPx->get(v); return v; }
	float getLineWidth() const { float v = 0.0f; _lineWidth->get(v); return v; }

	EdgeMode getEdgeMode() const {
		int v = 0;

		_edgeMode->get(v);

		return static_cast<EdgeMode>(v);
	}

	LineMode getLineMode() const {
		int v = 0;

		_lineMode->get(v);

		return static_cast<LineMode>(v);
	}

	osg::Vec4 getColorBg() const { osg::Vec4 v; _colorBg->get(v); return v; }
	osg::Vec4 getColorLine() const { osg::Vec4 v; _colorLine->get(v); return v; }
	osg::Vec4 getColorLineStrong() const { osg::Vec4 v; _colorLineStrong->get(v); return v; }

	// --- Fullscreen overlay wiring -----------------------------------------------------------

	// Wraps `this` in a Geode, and that Geode in an ABSOLUTE_RF, PRE_RENDER, ortho2D(-1,1,-1,1)
	// camera -- the "always drawn first, in the background" fullscreen NDC setup this class
	// exists for. PRE_RENDER runs before the viewer's own NESTED_RENDER camera regardless of
	// scene graph position; the viewport is deliberately left unset so it inherits the window's
	// current viewport and tracks resizes for free.
	//
	// The caller still needs to drop the viewer's main camera to GL_DEPTH_BUFFER_BIT-only
	// clearing, or its default COLOR_BUFFER_BIT clear will stomp this camera's paint:
	//
	//   viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT);
	osg::ref_ptr<osg::Camera> orthoCamera() {
		auto geode = make_ref<osg::Geode>();

		geode->addDrawable(this);

		auto camera = make_ref<osg::Camera>();

		camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
		camera->setRenderOrder(osg::Camera::PRE_RENDER);
		camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		camera->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		camera->setProjectionMatrix(osg::Matrix::ortho2D(-1.0, 1.0, -1.0, 1.0));
		camera->setViewMatrix(osg::Matrix::identity());
		camera->setAllowEventFocus(false);
		camera->setCullingActive(false);

		camera->addChild(geode);

		return camera;
	}

	// Skips the throwaway named instance: osgx::Grid::createOrthoCamera(args...) constructs a
	// Grid (forwarding args to whichever constructor matches) and immediately wraps it.
	template<typename... Args>
	static osg::ref_ptr<osg::Camera> createOrthoCamera(Args&&... args) {
		auto grid = make_ref<Grid>(std::forward<Args>(args)...);

		return grid->orthoCamera();
	}

private:
	void _build(const osg::Vec3& corner, const osg::Vec3& widthVec, const osg::Vec3& heightVec) {
		auto vertices = make_ref<osg::Vec3Array>();

		vertices->push_back(corner);
		vertices->push_back(corner + widthVec);
		vertices->push_back(corner + widthVec + heightVec);
		vertices->push_back(corner + heightVec);

		auto texCoords = make_ref<osg::Vec2Array>();

		texCoords->push_back(osg::Vec2(0.0f, 0.0f));
		texCoords->push_back(osg::Vec2(1.0f, 0.0f));
		texCoords->push_back(osg::Vec2(1.0f, 1.0f));
		texCoords->push_back(osg::Vec2(0.0f, 1.0f));

		auto normals = make_ref<osg::Vec3Array>();
		auto normal = widthVec ^ heightVec;

		normal.normalize();
		normals->push_back(normal);

		setVertexArray(vertices);
		setTexCoordArray(0, texCoords);
		setNormalArray(normals, osg::Array::BIND_OVERALL);
		addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

		_installState();
	}

	void _installState() {
		auto* ss = getOrCreateStateSet();
		auto program = make_ref<osg::Program>();

		program->addShader(new osg::Shader(osg::Shader::VERTEX, GRID_VERTEX_SHADER));
		program->addShader(new osg::Shader(osg::Shader::FRAGMENT, GRID_FRAGMENT_SHADER));

		ss->setAttributeAndModes(program, osg::StateAttribute::ON);
		ss->setMode(GL_BLEND, osg::StateAttribute::ON);
		// Enabling GL_BLEND alone leaves GL's default blend func (ONE, ZERO), which just
		// overwrites the destination regardless of alpha; SRC_ALPHA/ONE_MINUS_SRC_ALPHA is what
		// actually makes the zero-alpha background pixels transparent.
		ss->setAttributeAndModes(
			new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
			osg::StateAttribute::ON
		);

		_canvasSize = new osg::Uniform("u_canvasSize", osg::Vec2(300.0f, 300.0f));
		_gridInterval = new osg::Uniform("u_gridInterval", 5.0f);
		_gridIntervalStrong = new osg::Uniform("u_gridIntervalStrong", 10.0f);
		_lineWidthPx = new osg::Uniform("u_lineWidthPx", 1.0f);
		_lineWidth = new osg::Uniform("u_lineWidth", 0.5f);
		_edgeMode = new osg::Uniform("u_edgeMode", static_cast<int>(EDGE_ASIS));
		_lineMode = new osg::Uniform("u_lineMode", static_cast<int>(LINE_SCREEN_PIXELS));
		_colorBg = new osg::Uniform("u_colorBg", osg::Vec4(0.10f, 0.10f, 0.12f, 0.0f));
		_colorLine = new osg::Uniform("u_colorLine", osg::Vec4(0.45f, 0.45f, 0.50f, 1.0f));
		_colorLineStrong = new osg::Uniform("u_colorLineStrong", osg::Vec4(0.85f, 0.85f, 0.90f, 1.0f));

		ss->addUniform(_canvasSize);
		ss->addUniform(_gridInterval);
		ss->addUniform(_gridIntervalStrong);
		ss->addUniform(_lineWidthPx);
		ss->addUniform(_lineWidth);
		ss->addUniform(_edgeMode);
		ss->addUniform(_lineMode);
		ss->addUniform(_colorBg);
		ss->addUniform(_colorLine);
		ss->addUniform(_colorLineStrong);
	}

	// Re-locates uniform pointers from the (possibly newly-cloned) StateSet after a copy --
	// robust to both SHALLOW_COPY (StateSet shared, same uniform objects) and a deep-copy
	// CopyOp (StateSet cloned, uniforms cloned along with it).
	void _bindUniforms() {
		auto* ss = getOrCreateStateSet();

		_canvasSize = ss->getUniform("u_canvasSize");
		_gridInterval = ss->getUniform("u_gridInterval");
		_gridIntervalStrong = ss->getUniform("u_gridIntervalStrong");
		_lineWidthPx = ss->getUniform("u_lineWidthPx");
		_lineWidth = ss->getUniform("u_lineWidth");
		_edgeMode = ss->getUniform("u_edgeMode");
		_lineMode = ss->getUniform("u_lineMode");
		_colorBg = ss->getUniform("u_colorBg");
		_colorLine = ss->getUniform("u_colorLine");
		_colorLineStrong = ss->getUniform("u_colorLineStrong");
	}

	osg::ref_ptr<osg::Uniform> _canvasSize;
	osg::ref_ptr<osg::Uniform> _gridInterval;
	osg::ref_ptr<osg::Uniform> _gridIntervalStrong;
	osg::ref_ptr<osg::Uniform> _lineWidthPx;
	osg::ref_ptr<osg::Uniform> _lineWidth;
	osg::ref_ptr<osg::Uniform> _edgeMode;
	osg::ref_ptr<osg::Uniform> _lineMode;
	osg::ref_ptr<osg::Uniform> _colorBg;
	osg::ref_ptr<osg::Uniform> _colorLine;
	osg::ref_ptr<osg::Uniform> _colorLineStrong;
};

// ================================================================================================
// PBR / IBL
//
// Two namespaces, kept deliberately separate:
//
// osgx::pbr -- the BRDF math itself (GGX distribution, Schlick Fresnel, Smith geometry term).
// Independent of where the incoming light comes from: the same terms feed a direct point-light
// loop or an IBL environment term. Plain GLSL snippet constants, not full shaders or hooked
// shader objects -- see the namespace-level comment below for why.
//
// osgx::ibl -- the environment-as-light-source pipeline: a prefiltered specular cubemap plus a
// split-sum BRDF LUT (Karis 2013), and eventually SH-9 diffuse irradiance. Calls into osgx::pbr
// for its Fresnel term.
//
// Ported from the STATIC path of OpenSceneGraph.py/examples/pyosg-lighting/09-ibl.py: a
// pre-baked .ktx2 prefiltered cubemap loaded once, plus a one-shot BRDF LUT bake. Deliberately
// does NOT include 10-dynamicprobes.py's live GPU re-bake -- out of scope here.
// ================================================================================================

namespace pbr {

// GLSL function-body snippets, not full shaders -- concatenate the ones you need into a
// consuming fragment shader (same mechanism osgSlug's SHADER_LIB_FRAGMENT uses: paste the
// source in, above main()). All function names carry the osgx_ prefix to avoid collisions
// with whatever else is in the consuming shader.
//
// Contract: these assume `const float PI = 3.14159265359;` is already in scope. Not bundled
// here, since plenty of consuming shaders already define PI themselves and a duplicate
// `const float PI` is a compile error, not a harmless redefinition -- the caller adds it once.

// GGX/Trowbridge-Reitz normal distribution term (D). NdotH and roughness in [0,1];
// `roughness * roughness` is the standard Disney/Karis alpha remap.
inline constexpr const char* D_GGX = R"GLSL(
float osgx_D_GGX(float NdotH, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}
)GLSL";

// Schlick-GGX geometry term for a single direction (view OR light). Combine both via
// osgx_G_Smith (G_SMITH below) for the full geometric attenuation term.
inline constexpr const char* G_SCHLICK = R"GLSL(
float osgx_G_Schlick(float NdotX, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotX / (NdotX * (1.0 - k) + k);
}
)GLSL";

// Smith's method: visible geometric attenuation = product of the view-side and light-side
// Schlick-GGX terms. Requires osgx_G_Schlick (G_SCHLICK) already in scope.
inline constexpr const char* G_SMITH = R"GLSL(
float osgx_G_Smith(float NdotV, float NdotL, float roughness) {
	return osgx_G_Schlick(NdotV, roughness) * osgx_G_Schlick(NdotL, roughness);
}
)GLSL";

// Fresnel-Schlick: reflectance rises toward white (dielectrics) or the material's own tint
// (metals, via F0 = mix(vec3(0.04), albedo, metallic)) at grazing angles. For direct lights.
inline constexpr const char* F_SCHLICK = R"GLSL(
vec3 osgx_F_Schlick(float cosTheta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
)GLSL";

// Roughness-aware Fresnel (Lagarde) -- for IBL specular, so a rough surface's Fresnel rim
// doesn't stay mirror-sharp the way plain F_Schlick would. Direct lights use F_SCHLICK instead.
inline constexpr const char* F_SCHLICK_ROUGHNESS = R"GLSL(
vec3 osgx_F_Schlick_roughness(float cosTheta, vec3 F0, float roughness) {
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
)GLSL";

// All five snippets, concatenated in dependency order (G_SMITH calls osgx_G_Schlick, so
// G_SCHLICK must precede it). Convenience for callers that want the whole BRDF toolkit;
// reach for the individual constants instead if only part of it is needed.
inline std::string snippets() {
	return std::string(D_GGX) + G_SCHLICK + G_SMITH + F_SCHLICK + F_SCHLICK_ROUGHNESS;
}

// Per-light Cook-Torrance specular contribution (direct lighting), already multiplied by NdotL --
// caller multiplies by the light's own radiance (color * intensity/distance^2 or similar) and
// accumulates. Requires D_GGX/G_SCHLICK/G_SMITH/F_SCHLICK already in scope (include snippets()
// or those four individually before this one).
inline constexpr const char* DIRECT_SPECULAR = R"GLSL(
vec3 osgx_DirectSpecular(vec3 N, vec3 V, vec3 L, float NdotV, float roughness, vec3 F0) {
	float NdotL = max(dot(N, L), 0.0);

	if(NdotL <= 0.0) return vec3(0.0);

	vec3 H = normalize(L + V);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float D = osgx_D_GGX(NdotH, roughness);
	float G = osgx_G_Smith(NdotV, NdotL, roughness);
	vec3 F = osgx_F_Schlick(HdotV, F0);

	return (D * G * F * NdotL) / max(4.0 * NdotV * NdotL, 0.0001);
}
)GLSL";

// Split-sum IBL specular (Karis 2013): samples the prefiltered cubemap along the reflection
// vector and combines with the baked BRDF LUT. Handles the OSG (Z-up) -> baked-cubemap (Y-up)
// face remap internally. Self-contained -- does not require snippets() (the split-sum combine
// folds Fresnel into brdfLUT rather than calling F_SCHLICK_ROUGHNESS directly).
inline constexpr const char* IBL_SPECULAR = R"GLSL(
vec3 osgx_IBLSpecular(
	vec3 N,
	vec3 V,
	vec3 F0,
	float roughness,
	samplerCube envMap,
	sampler2D brdfLUT,
	float envMaxMip
) {
	vec3 R = reflect(-V, N);
	float NdotV = max(dot(N, V), 0.0);

	// OSG world space is Z-up; the baked cubemap's faces are Y-up -- without this remap we'd
	// sample a direction that doesn't correspond to R at all.
	vec3 R_gl = vec3(R.x, R.z, -R.y);

	vec3 prefilt = textureLod(envMap, R_gl, roughness * envMaxMip).rgb;
	vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

	return prefilt * (F0 * brdf.x + brdf.y);
}
)GLSL";

// Khronos PBR Neutral tonemap -- hue-preserving (no ACES orange shift), for compressing HDR
// specular (routinely > 1.0 off a near-mirror surface under a bright environment) into LDR
// without hard-clipping to solid white. Ported verbatim from 09-ibl.py's tonemapPBRNeutral().
// Caller still applies its own gamma afterward if not rendering to an sRGB framebuffer.
inline constexpr const char* TONEMAP_PBR_NEUTRAL = R"GLSL(
vec3 osgx_TonemapPBRNeutral(vec3 color) {
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if(peak >= startCompression) {
		float d = 1.0 - startCompression;
		float newPeak = 1.0 - d * d / (peak + d - startCompression);
		color *= newPeak / peak;
		float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
		color = mix(color, vec3(newPeak), g);
	}
	return clamp(color, 0.0, 1.0);
}
)GLSL";

// Animates a handful of point lights orbiting a center point, writing world-space
// position+intensity into a vec4 array uniform ("lightPosIntensity" by default) every update
// traversal -- the motion is what confirms N/V/specular are wired correctly rather than just a
// static flat-shaded color. Install as the update callback on whichever node the lit shape hangs
// from; `ss` must be the StateSet holding that uniform (array size >= orbits.size()).
//
// Reusable across any osgx::pbr-lit scene -- configure `center`/`orbits`/`intensity` per use
// instead of copying this callback into each consumer.
struct OrbitLightRig: public osg::NodeCallback {
	struct Orbit {
		float radius, height, speed, phase, intensity;
	};

	osg::ref_ptr<osg::StateSet> ss;
	osg::Vec3 center{0.0f, 0.0f, 0.0f};
	float intensity = 1.0f; // global scale, e.g. a --light-intensity CLI flag
	std::string uniformName = "lightPosIntensity";

	// Default rig: (orbit radius, height above center, angular speed, phase, per-orbit intensity).
	// Matches the original osgslug-pbr-ibl.cpp badge rig; override for a different look.
	std::vector<Orbit> orbits = {
		{0.55f, 0.70f, 0.50f, 0.0f, 1.00f},
		{0.70f, 0.90f, -0.33f, 2.1f, 0.75f},
		{0.45f, 0.50f, 0.80f, 4.2f, 0.50f},
	};

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		float t = nv->getFrameStamp() ? float(nv->getFrameStamp()->getSimulationTime()) : 0.0f;
		auto* lp = ss->getUniform(uniformName);

		for(size_t i = 0; i < orbits.size(); i++) {
			const auto& o = orbits[i];
			float a = t * o.speed + o.phase;

			lp->setElement(static_cast<unsigned int>(i), osg::Vec4(
				center.x() + std::cos(a) * o.radius,
				center.y() + std::sin(a) * o.radius,
				center.z() + o.height,
				o.intensity * intensity
			));
		}

		traverse(node, nv);
	}
};

}

namespace ibl {

// Disables a node after its update callback has fired exactly once -- e.g. a PRE_RENDER bake
// camera that should render one frame at startup and then go idle. Call rebake() to re-arm it
// (render one more frame -- e.g. after swapping the bake's source data).
class RunOnceCallback: public osg::NodeCallback {
public:
	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		if(_done) node->setNodeMask(0);

		_done = true;

		traverse(node, nv);
	}

	void rebake(osg::Node* node) {
		node->setNodeMask(0xFFFFFFFF);
		_done = false;
	}

private:
	bool _done = false;
};

// Fullscreen NDC-quad vertex shader -- shared by any single-pass bake (BRDF LUT today; future
// bakes that need a rasterized pass can reuse it too).
inline constexpr const char* FULLSCREEN_VERT = R"GLSL(
#version 330 core
in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;
out vec2 vUV;
void main() {
	vUV = osg_MultiTexCoord0;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)GLSL";

// Split-sum BRDF LUT bake (Karis 2013) -- environment-independent, so it only ever needs to
// bake once. R channel = Fresnel scale, G channel = Fresnel bias; sampled in the consuming
// shader as texture(brdfLUT, vec2(NdotV, roughness)).rg.
inline constexpr const char* BRDF_LUT_FRAG = R"GLSL(
#version 330 core
const float PI = 3.14159265359;
in vec2 vUV;
out vec4 fragColor;

float radicalInverseVdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
	return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, float roughness) {
	float a = roughness * roughness;
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float G_GGX_IBL(float NdotX, float roughness) {
	float k = (roughness * roughness) / 2.0;
	return NdotX / (NdotX * (1.0 - k) + k);
}

void main() {
	float NdotV = max(vUV.x, 1e-4);
	float roughness = vUV.y;
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

	float scale = 0.0, bias = 0.0;
	const uint SAMPLES = 1024u;

	for(uint i = 0u; i < SAMPLES; i++) {
		vec2 Xi = hammersley(i, SAMPLES);
		vec3 H = importanceSampleGGX(Xi, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);

		if(NdotL > 0.0) {
			float G = G_GGX_IBL(NdotV, roughness) * G_GGX_IBL(NdotL, roughness);
			float G_vis = G * VdotH / max(NdotH * NdotV, 0.001);
			float Fc = pow(1.0 - VdotH, 5.0);

			scale += (1.0 - Fc) * G_vis;
			bias += Fc * G_vis;
		}
	}

	fragColor = vec4(scale / float(SAMPLES), bias / float(SAMPLES), 0.0, 1.0);
}
)GLSL";

// Loads a pre-baked GGX-prefiltered cubemap from a .ktx2 file (see osgGLTF's
// ReaderWriterKTX2.cpp for the plugin that makes this format readable -- must be registered
// with osgDB, same as any other reader/writer plugin). The KTX2 is expected to carry its own
// hand-baked mip chain, one level per roughness step -- hardware mipmap generation is disabled
// so OSG doesn't overwrite it.
//
// Returns nullptr (and logs via OSG_WARN) if the path doesn't load, or doesn't load as a
// TextureCubeMap.
inline osg::ref_ptr<osg::TextureCubeMap> loadPrefilterCubemap(const std::string& path) {
	osg::ref_ptr<osg::Object> obj = osgDB::readRefObjectFile(path);
	auto* cube = dynamic_cast<osg::TextureCubeMap*>(obj.get());

	if(!cube) {
		OSG_WARN <<
			"osgx::ibl::loadPrefilterCubemap: " << path <<
			" did not load as a TextureCubeMap" << std::endl
		;

		return nullptr;
	}

	cube->setUseHardwareMipMapGeneration(false);

	return cube;
}

// Creates a PRE_RENDER FBO camera that bakes the split-sum BRDF LUT into `lut` exactly once
// (via RunOnceCallback -- installed as the camera's update callback). `lut` is configured
// in-place (size/format/filters), matching the out-param convention used by the two-argument
// makePickCamera() overload above. The caller is responsible for:
//
// - adding the returned camera as a child of the scene graph (anywhere -- it's ABSOLUTE_RF)
// - NOT expecting it to re-bake on its own: the LUT's only inputs are NdotV and roughness,
//   both baked into the UV axes, so a static environment never needs a second bake. Call
//   rebake() on the camera's RunOnceCallback (via getUpdateCallback()) if that ever changes.
inline osg::ref_ptr<osg::Camera> makeBRDFLUTCamera(int lutSize, osg::Texture2D* lut) {
	lut->setTextureSize(lutSize, lutSize);
	lut->setInternalFormat(GL_RGBA);
	lut->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	lut->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	lut->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	lut->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	auto prog = make_ref<osg::Program>();

	prog->setName("osgx_ibl_brdfLutBake");
	prog->addShader(new osg::Shader(osg::Shader::VERTEX, FULLSCREEN_VERT));
	prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, BRDF_LUT_FRAG));

	auto quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0)
	);

	auto geode = make_ref<osg::Geode>();

	geode->addDrawable(quad);

	auto cam = make_ref<osg::Camera>();

	cam->setName("osgx_ibl_BRDFLUTBake");
	cam->setRenderOrder(osg::Camera::PRE_RENDER);
	cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	cam->setClearMask(GL_COLOR_BUFFER_BIT);
	cam->setViewport(0, 0, lutSize, lutSize);
	cam->setProjectionMatrix(osg::Matrix::identity());
	cam->setViewMatrix(osg::Matrix::identity());
	cam->attach(osg::Camera::COLOR_BUFFER0, lut);
	cam->getOrCreateStateSet()->setAttributeAndModes(prog, osg::StateAttribute::ON);
	cam->addChild(geode);
	cam->setUpdateCallback(new RunOnceCallback());

	return cam;
}

// ------------------------------------------------------------------------------------------------
// SH-9 diffuse irradiance
//
// L0-L2 spherical harmonics: 9 RGB coefficients standing in for the whole low-frequency diffuse
// environment -- much cheaper than sampling a cubemap per-pixel for diffuse, at the cost of only
// capturing broad/blurry lighting (which is all diffuse irradiance ever needs). Ported from
// 09-ibl.py's compute_sh() (projection) and sh_irradiance() (GLSL evaluation).
// ------------------------------------------------------------------------------------------------

struct SH9 {
	osg::Vec3f coeffs[9];
};

// Projects an equirectangular (2:1) HDR/LDR environment image onto SH9. Cosine-lobe A_l weights
// are baked in here so the GLSL evaluation (SH_IRRADIANCE below) is a plain dot-product sum.
//
// O(width*height) -- meant to run once at startup (or once per environment swap), not per frame.
// img's pixel format is read via osg::Image::getColor(), which returns true (unnormalized) float
// radiance for float-format images -- use a genuinely HDR-loaded osg::Image (e.g. a .hdr file via
// osgDB::readImageFile()), not an LDR-clamped one, or the diffuse term will be dim/wrong.
inline SH9 computeSH(const osg::Image* img) {
	SH9 sh;

	const auto W = static_cast<std::size_t>(std::max(img->s(), 0));
	const auto H = static_cast<std::size_t>(std::max(img->t(), 0));

	for(std::size_t y = 0; y < H; y++) {
		const double theta = (double(y) + 0.5) / double(H) * osg::PI;
		const double sinTheta = std::sin(theta);
		const double cosTheta = std::cos(theta);
		const double dOmega = sinTheta * (osg::PI / double(H)) * (2.0 * osg::PI / double(W));

		for(std::size_t x = 0; x < W; x++) {
			const double phi = (double(x) + 0.5) / double(W) * 2.0 * osg::PI;
			const double sx = sinTheta * std::cos(phi);
			const double sy = sinTheta * std::sin(phi);
			const double sz = cosTheta;

			const double Y[9] = {
				0.282095,
				0.488603 * sy,
				0.488603 * sz,
				0.488603 * sx,
				1.092548 * sx * sy,
				1.092548 * sy * sz,
				0.315392 * (3.0 * sz * sz - 1.0),
				1.092548 * sx * sz,
				0.546274 * (sx * sx - sy * sy)
			};

			const double A[9] = {
				osg::PI,
				2.0 * osg::PI / 3.0,
				2.0 * osg::PI / 3.0,
				2.0 * osg::PI / 3.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0
			};

			const osg::Vec4f c = img->getColor(
				static_cast<unsigned int>(x),
				static_cast<unsigned int>(y)
			);

			for(int i = 0; i < 9; i++) {
				const double w = Y[i] * A[i] * dOmega;

				sh.coeffs[i] += osg::Vec3f(
					float(double(c.r()) * w),
					float(double(c.g()) * w),
					float(double(c.b()) * w)
				);
			}
		}
	}

	return sh;
}

// GLSL evaluation of an SH9 environment at world-space normal N. shCoeffs is a 9-element array
// uniform (or local) -- caller declares and binds it under whatever name fits their shader
// (e.g. `uniform vec3 iblSH[9];`), then calls osgx_SHIrradiance(N, iblSH).
inline constexpr const char* SH_IRRADIANCE = R"GLSL(
vec3 osgx_SHIrradiance(vec3 N, vec3 shCoeffs[9]) {
	return max(
		shCoeffs[0]
		+ shCoeffs[1] * N.y + shCoeffs[2] * N.z + shCoeffs[3] * N.x
		+ shCoeffs[4] * N.x * N.y + shCoeffs[5] * N.y * N.z
		+ shCoeffs[6] * (3.0 * N.z * N.z - 1.0)
		+ shCoeffs[7] * N.x * N.z + shCoeffs[8] * (N.x * N.x - N.y * N.y),
		vec3(0.0)
	);
}
)GLSL";

}

namespace detail {

struct ShaderLibEntry {
	std::string_view name;
	std::string_view glslName;
	std::string_view source;
};

inline std::string_view trim(std::string_view text) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c); };

	while(!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
		text.remove_prefix(1);
	}

	while(!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
		text.remove_suffix(1);
	}

	return text;
}

inline bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
	if(text.size() < prefix.size()) return false;

	return std::equal(prefix.begin(), prefix.end(), text.begin(), [](
		const char lhs,
		const char rhs
	) {
		return std::tolower(static_cast<unsigned char>(lhs)) ==
			std::tolower(static_cast<unsigned char>(rhs));
	});
}

inline std::string normalizeShaderLibName(std::string_view name) {
	name = trim(name);

	if(startsWithIgnoringCase(name, "osgx_")) name.remove_prefix(5);

	std::string normalized;

	normalized.reserve(name.size());

	for(const auto c : name) {
		if(std::isalnum(static_cast<unsigned char>(c))) {
			normalized.push_back(
				static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
			);
		}
	}

	return normalized;
}

inline bool shaderLibNameMatches(std::string_view requested, const ShaderLibEntry& entry) {
	const auto normalized = normalizeShaderLibName(requested);

	return normalized == normalizeShaderLibName(entry.name) ||
		normalized == normalizeShaderLibName(entry.glslName);
}

inline std::vector<std::string_view> splitShaderLibNames(std::string_view names) {
	if(const auto comment = names.find("//"); comment != std::string_view::npos) {
		names = names.substr(0, comment);
	}

	std::vector<std::string_view> tokens;

	for(std::string_view::size_type pos = 0; pos <= names.size();) {
		const auto comma = names.find(',', pos);
		const auto end = comma == std::string_view::npos ? names.size() : comma;
		const auto token = trim(names.substr(pos, end - pos));

		if(!token.empty()) tokens.push_back(token);
		if(comma == std::string_view::npos) break;

		pos = comma + 1;
	}

	return tokens;
}

inline const auto& pbrShaderLibs() {
	static const std::array<ShaderLibEntry, 8> libs = {{
		{"D_GGX", "osgx_D_GGX", pbr::D_GGX},
		{"G_SCHLICK", "osgx_G_Schlick", pbr::G_SCHLICK},
		{"G_SMITH", "osgx_G_Smith", pbr::G_SMITH},
		{"F_SCHLICK", "osgx_F_Schlick", pbr::F_SCHLICK},
		{"F_SCHLICK_ROUGHNESS", "osgx_F_Schlick_roughness", pbr::F_SCHLICK_ROUGHNESS},
		{"DIRECT_SPECULAR", "osgx_DirectSpecular", pbr::DIRECT_SPECULAR},
		{"IBL_SPECULAR", "osgx_IBLSpecular", pbr::IBL_SPECULAR},
		{"TONEMAP_PBR_NEUTRAL", "osgx_TonemapPBRNeutral", pbr::TONEMAP_PBR_NEUTRAL}
	}};

	return libs;
}

inline const auto& iblShaderLibs() {
	static const std::array<ShaderLibEntry, 1> libs = {{
		{"SH_IRRADIANCE", "osgx_SHIrradiance", ibl::SH_IRRADIANCE}
	}};

	return libs;
}

template<typename Libs>
std::string resolveShaderLibs(std::string_view namespaceName, std::string_view names, const Libs& libs) {
	const auto tokens = splitShaderLibNames(names);

	if(tokens.empty()) {
		throw std::runtime_error(
			"osgx::resolveLibs: empty #pragma osgx::" + std::string(namespaceName)
		);
	}

	const auto includeAll = tokens.size() == 1 && trim(tokens.front()) == "*";
	std::string resolved;

	for(const auto& lib : libs) {
		const auto requested = includeAll || std::any_of(tokens.begin(), tokens.end(), [&lib](auto token) {
			return shaderLibNameMatches(token, lib);
		});

		if(requested) resolved += lib.source;
	}

	if(!includeAll) {
		for(const auto token : tokens) {
			const auto found = std::any_of(libs.begin(), libs.end(), [token](const auto& lib) {
				return shaderLibNameMatches(token, lib);
			});

			if(!found) {
				throw std::runtime_error(
					"osgx::resolveLibs: unknown #pragma osgx::" +
					std::string(namespaceName) + " lib '" + std::string(token) + "'"
				);
			}
		}
	}

	return resolved;
}

inline std::optional<std::string> resolveShaderLibPragma(std::string_view line) {
	auto rest = trim(line);

	if(!startsWithIgnoringCase(rest, "#pragma")) return std::nullopt;

	rest.remove_prefix(std::string_view("#pragma").size());
	rest = trim(rest);

	if(!startsWithIgnoringCase(rest, "osgx::")) return std::nullopt;

	rest.remove_prefix(std::string_view("osgx::").size());

	const auto nsEnd = std::find_if(rest.begin(), rest.end(), [](const char c) {
		return std::isspace(static_cast<unsigned char>(c));
	});
	const auto nsSize = static_cast<std::string_view::size_type>(std::distance(rest.begin(), nsEnd));

	const auto namespaceName = rest.substr(0, nsSize);
	const auto names = rest.substr(nsSize);

	if(startsWithIgnoringCase(namespaceName, "pbr") && namespaceName.size() == 3) {
		return resolveShaderLibs(namespaceName, names, pbrShaderLibs());
	}

	if(startsWithIgnoringCase(namespaceName, "ibl") && namespaceName.size() == 3) {
		return resolveShaderLibs(namespaceName, names, iblShaderLibs());
	}

	throw std::runtime_error(
		"osgx::resolveLibs: unknown #pragma osgx namespace '" +
		std::string(namespaceName) + "'"
	);
}

}

// Replaces line-oriented shader library pragmas with their GLSL source. Examples:
//
//   #pragma osgx::pbr D_GGX,G_SCHLICK,G_SMITH
//   #pragma osgx::pbr *
//   #pragma osgx::ibl SH_IRRADIANCE
//
// Names are case-insensitive, and may use either the C++ catalog name (`F_SCHLICK`) or the
// GLSL function name (`osgx_F_Schlick`). `*` expands the whole namespace catalog in dependency
// order. Unknown osgx pragmas throw, so shader typos fail close to the source string.
inline std::string resolveLibs(std::string src) {
	std::string resolved;

	resolved.reserve(src.size());

	for(std::string::size_type pos = 0; pos < src.size();) {
		const auto lineEnd = src.find_first_of("\r\n", pos);
		const auto lineSize = lineEnd == std::string::npos ? src.size() - pos : lineEnd - pos;
		const std::string_view line(src.data() + pos, lineSize);

		if(auto replacement = detail::resolveShaderLibPragma(line)) resolved += *replacement;
		else resolved.append(line);

		if(lineEnd == std::string::npos) break;

		if(src[lineEnd] == '\r' && lineEnd + 1 < src.size() && src[lineEnd + 1] == '\n') {
			resolved.append("\r\n");
			pos = lineEnd + 2;
		}
		else {
			resolved.push_back(src[lineEnd]);
			pos = lineEnd + 1;
		}
	}

	return resolved;
}

}
