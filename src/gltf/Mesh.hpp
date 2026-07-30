#pragma once

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Array>
#include <osg/Group>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <vector>

namespace osgDB { class Options; }
namespace tinygltf {
class Model;
struct Mesh;
}

namespace osgx::gltf::detail {

class MaterialBuilder;
struct Skin;

class MeshBuilder {
public:
	MeshBuilder(
		const tinygltf::Model& model,
		const osgDB::Options* readOptions,
		MaterialBuilder& materialBuilder,
		const std::vector<osg::ref_ptr<osg::Array>>& arrays,
		const std::vector<osg::ref_ptr<Skin>>& skins
	);

	osg::Group* makeMesh(const tinygltf::Mesh& mesh, int skinIndex) const;

private:
	const tinygltf::Model& _model;
	const osgDB::Options* _readOptions;
	MaterialBuilder& _materialBuilder;
	const std::vector<osg::ref_ptr<osg::Array>>& _arrays;
	const std::vector<osg::ref_ptr<Skin>>& _skins;

	static GLenum _primitiveMode(int gltfMode);
};

}
