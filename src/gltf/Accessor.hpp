#pragma once

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Array>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <vector>

namespace tinygltf { class Model; }

namespace osgx::gltf::detail {

std::vector<osg::ref_ptr<osg::Array>> extractArrays(const tinygltf::Model& model);

}
