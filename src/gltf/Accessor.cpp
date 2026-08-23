#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include "tiny_gltf_v3.h"

OSGX_ENABLE_WARNINGS

#include "Accessor.hpp"
#include "Log.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace osgx::gltf::detail {
namespace {

template<typename OSGArray, int ComponentType, int AccessorType>
OSGArray* makeTypedArray(
	const tg3_buffer& buffer,
	const tg3_buffer_view& bufferView,
	const tg3_accessor& accessor
) {
	if(accessor.count > std::numeric_limits<unsigned int>::max()) return nullptr;

	auto* array = new OSGArray(static_cast<unsigned int>(accessor.count));

	if(accessor.count == 0) return array;

	const std::int32_t componentSize = tg3_component_size(ComponentType);
	const std::int32_t componentCount = tg3_num_components(AccessorType);
	const std::size_t elementSize = static_cast<std::size_t>(componentSize * componentCount);
	const std::size_t sourceStride = bufferView.byte_stride == 0
		? elementSize
		: bufferView.byte_stride
	;
	const unsigned char* source =
		buffer.data.data + bufferView.byte_offset + accessor.byte_offset;

	if constexpr(std::is_same_v<typename OSGArray::value_type, osg::Matrixf>) {
		for(std::size_t i = 0; i < accessor.count; i++, source += sourceStride) {
			float values[16];

			std::memcpy(values, source, sizeof(values));
			(*array)[i].set(values);
		}
	}
	else if(bufferView.byte_stride == 0) {
		std::memcpy(&(*array)[0], source, elementSize * accessor.count);
	}
	else {
		for(std::size_t i = 0; i < accessor.count; i++, source += bufferView.byte_stride) {
			std::memcpy(&(*array)[i], source, elementSize);
		}
	}

	return array;
}

template<
	int ComponentType,
	typename ScalarArray,
	typename Vec2Array,
	typename Vec3Array,
	typename Vec4Array
>
osg::Array* makeVectorArray(
	const tg3_buffer& buffer,
	const tg3_buffer_view& bufferView,
	const tg3_accessor& accessor
) {
	switch(accessor.type) {
		case TG3_TYPE_SCALAR:
			return makeTypedArray<ScalarArray, ComponentType, TG3_TYPE_SCALAR>(
				buffer,
				bufferView,
				accessor
			);
		case TG3_TYPE_VEC2:
			return makeTypedArray<Vec2Array, ComponentType, TG3_TYPE_VEC2>(
				buffer,
				bufferView,
				accessor
			);
		case TG3_TYPE_VEC3:
			return makeTypedArray<Vec3Array, ComponentType, TG3_TYPE_VEC3>(
				buffer,
				bufferView,
				accessor
			);
		case TG3_TYPE_VEC4:
			return makeTypedArray<Vec4Array, ComponentType, TG3_TYPE_VEC4>(
				buffer,
				bufferView,
				accessor
			);
		default: return nullptr;
	}
}

osg::Array* makeArray(
	const tg3_buffer& buffer,
	const tg3_buffer_view& bufferView,
	const tg3_accessor& accessor
) {
	switch(accessor.component_type) {
		case TG3_COMPONENT_TYPE_BYTE:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_BYTE,
				osg::ByteArray,
				osg::Vec2bArray,
				osg::Vec3bArray,
				osg::Vec4bArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_UNSIGNED_BYTE,
				osg::UByteArray,
				osg::Vec2ubArray,
				osg::Vec3ubArray,
				osg::Vec4ubArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_SHORT:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_SHORT,
				osg::ShortArray,
				osg::Vec2sArray,
				osg::Vec3sArray,
				osg::Vec4sArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_UNSIGNED_SHORT,
				osg::UShortArray,
				osg::Vec2usArray,
				osg::Vec3usArray,
				osg::Vec4usArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_INT:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_INT,
				osg::IntArray,
				osg::Vec2iArray,
				osg::Vec3iArray,
				osg::Vec4iArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_UNSIGNED_INT:
			return makeVectorArray<
				TG3_COMPONENT_TYPE_UNSIGNED_INT,
				osg::UIntArray,
				osg::Vec2uiArray,
				osg::Vec3uiArray,
				osg::Vec4uiArray
			>(buffer, bufferView, accessor);

		case TG3_COMPONENT_TYPE_FLOAT:
			if(accessor.type == TG3_TYPE_MAT4) {
				return makeTypedArray<
					osg::MatrixfArray,
					TG3_COMPONENT_TYPE_FLOAT,
					TG3_TYPE_MAT4
				>(buffer, bufferView, accessor);
			}

			return makeVectorArray<
				TG3_COMPONENT_TYPE_FLOAT,
				osg::FloatArray,
				osg::Vec2Array,
				osg::Vec3Array,
				osg::Vec4Array
			>(buffer, bufferView, accessor);

		default: return nullptr;
	}
}

}

std::vector<osg::ref_ptr<osg::Array>> extractArrays(const tg3_model& model) {
	std::vector<osg::ref_ptr<osg::Array>> arrays;

	arrays.reserve(model.accessors_count);

	GLTF_NOTIFY(0)
		<< "extractArrays - " << model.accessors_count << " accessor(s)" << std::endl
	;

	for(std::uint32_t accessorIndex = 0; accessorIndex < model.accessors_count; accessorIndex++) {
		const tg3_accessor& accessor = model.accessors[accessorIndex];

		GLTF_NOTIFY(1)
			<< "accessor[" << accessorIndex << "]"
			<< " componentType=" << accessor.component_type
			<< " type=" << accessor.type
			<< " count=" << accessor.count
			<< " bufferView=" << accessor.buffer_view << std::endl
		;

		// Accessors without a bufferView are valid, for example when sparse base data is implicitly
		// zero. Preserve a null entry so OSG-array indices continue to match glTF accessor indices.
		if(
			accessor.buffer_view < 0 ||
			static_cast<std::uint32_t>(accessor.buffer_view) >= model.buffer_views_count
		) {
			GLTF_NOTIFY(2) << "-> no bufferView, skipping" << std::endl;
			arrays.push_back({});

			continue;
		}

		const tg3_buffer_view& bufferView =
			model.buffer_views[static_cast<std::uint32_t>(accessor.buffer_view)];

		if(bufferView.buffer < 0 || static_cast<std::uint32_t>(bufferView.buffer) >= model.buffers_count) {
			GLTF_NOTIFY(2) << "-> invalid buffer, skipping" << std::endl;
			arrays.push_back({});

			continue;
		}

		const tg3_buffer& buffer = model.buffers[static_cast<std::uint32_t>(bufferView.buffer)];
		osg::ref_ptr<osg::Array> array = makeArray(buffer, bufferView, accessor);

		if(array) {
			array->setBinding(osg::Array::BIND_PER_VERTEX);
			array->setNormalize(accessor.normalized);

			GLTF_NOTIFY(2)
				<< "-> built array, " << array->getNumElements() << " element(s)" << std::endl
			;
		}
		else {
			GLTF_NOTIFY(2) << "-> no array built (unhandled type combination)" << std::endl;
		}

		arrays.push_back(array);
	}

	GLTF_NOTIFY(0) << "extractArrays done - " << arrays.size() << " array(s) built" << std::endl;

	return arrays;
}

}
