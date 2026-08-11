#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

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
	const tinygltf::Buffer& buffer,
	const tinygltf::BufferView& bufferView,
	const tinygltf::Accessor& accessor
) {
	if(accessor.count > std::numeric_limits<unsigned int>::max()) return nullptr;

	auto* array = new OSGArray(static_cast<unsigned int>(accessor.count));

	if(accessor.count == 0) return array;

	const std::int32_t componentSize = tinygltf::GetComponentSizeInBytes(ComponentType);
	const std::int32_t componentCount = tinygltf::GetNumComponentsInType(AccessorType);
	const std::size_t elementSize = static_cast<std::size_t>(componentSize * componentCount);
	const std::size_t sourceStride = bufferView.byteStride == 0
		? elementSize
		: bufferView.byteStride
	;
	const unsigned char* source =
		buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

	if constexpr(std::is_same_v<typename OSGArray::value_type, osg::Matrixf>) {
		for(std::size_t i = 0; i < accessor.count; i++, source += sourceStride) {
			float values[16];

			std::memcpy(values, source, sizeof(values));
			(*array)[i].set(values);
		}
	}
	else if(bufferView.byteStride == 0) {
		std::memcpy(&(*array)[0], source, elementSize * accessor.count);
	}
	else {
		for(std::size_t i = 0; i < accessor.count; i++, source += bufferView.byteStride) {
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
	const tinygltf::Buffer& buffer,
	const tinygltf::BufferView& bufferView,
	const tinygltf::Accessor& accessor
) {
	switch(accessor.type) {
		case TINYGLTF_TYPE_SCALAR:
			return makeTypedArray<ScalarArray, ComponentType, TINYGLTF_TYPE_SCALAR>(
				buffer,
				bufferView,
				accessor
			);
		case TINYGLTF_TYPE_VEC2:
			return makeTypedArray<Vec2Array, ComponentType, TINYGLTF_TYPE_VEC2>(
				buffer,
				bufferView,
				accessor
			);
		case TINYGLTF_TYPE_VEC3:
			return makeTypedArray<Vec3Array, ComponentType, TINYGLTF_TYPE_VEC3>(
				buffer,
				bufferView,
				accessor
			);
		case TINYGLTF_TYPE_VEC4:
			return makeTypedArray<Vec4Array, ComponentType, TINYGLTF_TYPE_VEC4>(
				buffer,
				bufferView,
				accessor
			);
		default: return nullptr;
	}
}

osg::Array* makeArray(
	const tinygltf::Buffer& buffer,
	const tinygltf::BufferView& bufferView,
	const tinygltf::Accessor& accessor
) {
	switch(accessor.componentType) {
		case TINYGLTF_COMPONENT_TYPE_BYTE:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_BYTE,
				osg::ByteArray,
				osg::Vec2bArray,
				osg::Vec3bArray,
				osg::Vec4bArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE,
				osg::UByteArray,
				osg::Vec2ubArray,
				osg::Vec3ubArray,
				osg::Vec4ubArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_SHORT:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_SHORT,
				osg::ShortArray,
				osg::Vec2sArray,
				osg::Vec3sArray,
				osg::Vec4sArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT,
				osg::UShortArray,
				osg::Vec2usArray,
				osg::Vec3usArray,
				osg::Vec4usArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_INT:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_INT,
				osg::IntArray,
				osg::Vec2iArray,
				osg::Vec3iArray,
				osg::Vec4iArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
				osg::UIntArray,
				osg::Vec2uiArray,
				osg::Vec3uiArray,
				osg::Vec4uiArray
			>(buffer, bufferView, accessor);

		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			if(accessor.type == TINYGLTF_TYPE_MAT4) {
				return makeTypedArray<
					osg::MatrixfArray,
					TINYGLTF_COMPONENT_TYPE_FLOAT,
					TINYGLTF_TYPE_MAT4
				>(buffer, bufferView, accessor);
			}

			return makeVectorArray<
				TINYGLTF_COMPONENT_TYPE_FLOAT,
				osg::FloatArray,
				osg::Vec2Array,
				osg::Vec3Array,
				osg::Vec4Array
			>(buffer, bufferView, accessor);

		default: return nullptr;
	}
}

}

std::vector<osg::ref_ptr<osg::Array>> extractArrays(const tinygltf::Model& model) {
	std::vector<osg::ref_ptr<osg::Array>> arrays;

	arrays.reserve(model.accessors.size());

	GLTF_NOTIFY(0)
		<< "extractArrays - " << model.accessors.size() << " accessor(s)" << std::endl
	;

	for(std::size_t accessorIndex = 0; accessorIndex < model.accessors.size(); accessorIndex++) {
		const tinygltf::Accessor& accessor = model.accessors[accessorIndex];

		GLTF_NOTIFY(1)
			<< "accessor[" << accessorIndex << "]"
			<< " componentType=" << accessor.componentType
			<< " type=" << accessor.type
			<< " count=" << accessor.count
			<< " bufferView=" << accessor.bufferView << std::endl
		;

		// Accessors without a bufferView are valid, for example when sparse base data is implicitly
		// zero. Preserve a null entry so OSG-array indices continue to match glTF accessor indices.
		if(
			accessor.bufferView < 0 ||
			accessor.bufferView >= static_cast<int>(model.bufferViews.size())
		) {
			GLTF_NOTIFY(2) << "-> no bufferView, skipping" << std::endl;
			arrays.push_back({});

			continue;
		}

		const std::size_t bufferViewIndex = static_cast<std::size_t>(accessor.bufferView);
		const tinygltf::BufferView& bufferView = model.bufferViews[bufferViewIndex];

		if(bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
			GLTF_NOTIFY(2) << "-> invalid buffer, skipping" << std::endl;
			arrays.push_back({});

			continue;
		}

		const std::size_t bufferIndex = static_cast<std::size_t>(bufferView.buffer);
		const tinygltf::Buffer& buffer = model.buffers[bufferIndex];
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
