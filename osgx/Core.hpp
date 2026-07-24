#pragma once

#include "Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Node>
#include <osg/Object>
#include <osg/Referenced>
#include <osg/Timer>
#include <osg/Vec3>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <array>
#include <concepts>
#include <functional>
#include <limits>
#include <list>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// The same as `META_Object` but updated for modern C++ warnings.
#define OSGX_META_Object(library,name) \
	osg::Object* cloneType() const override { return new name (); } \
	osg::Object* clone(const osg::CopyOp& copyop) const override { return new name (*this,copyop); } \
	bool isSameKindAs(const osg::Object* obj) const override { return dynamic_cast<const name *>(obj)!=NULL; } \
	const char* libraryName() const override { return #library; }\
	const char* className() const override { return #name; }

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


}
