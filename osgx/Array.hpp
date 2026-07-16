#pragma once

#include "Core.hpp"

namespace osgx {

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

}
