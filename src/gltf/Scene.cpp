#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS

#include "Scene.hpp"
#include "Accessor.hpp"
#include "Animation.hpp"
#include "Log.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Skin.hpp"
#include "Texture.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/CullFace>
#include <osg/MatrixTransform>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <osgDB/Options>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <vector>

namespace osgx::gltf::detail {

class SceneBuilder {
public:
	SceneBuilder(
		const tinygltf::Model& model,
		const std::string& referrer,
		const osgDB::Options* readOptions,
		TextureCache* textureCache,
		const Reader::ProgressCallback& progress
	):
	_model(model),
	_readOptions(readOptions),
	_textureLoader(model, referrer, readOptions, textureCache),
	_materialBuilder(model, referrer, readOptions, _textureLoader),
	_progress(progress),
	_meshBuilder(model, readOptions, _materialBuilder, _arrays, _skins) {
		_nodeTransforms.resize(model.nodes.size());

		_arrays = extractArrays(model);
		_skins = prepareSkins(model, _arrays);
	}

	osg::Node* build() {
		// glTF is Y-up; rotate to Z-up unless the caller passes "gltfZUp".
		bool zUp =
			_readOptions &&
			_readOptions->getOptionString().find("gltfZUp") != std::string::npos
		;

		osg::MatrixTransform* root = new osg::MatrixTransform();

		if(!zUp) root->setMatrix(osg::Matrixd::rotate(
			osg::Vec3d(0, 1, 0),
			osg::Vec3d(0, 0, 1)
		));

		for(auto& scene : _model.scenes) {
			for(int idx : scene.nodes) {
				if(osg::Node* node = _createNode(idx)) root->addChild(node);
			}
		}

		resolveSkinJointNodes(_model, _nodeTransforms, _skins);
		_installAnimationCallback(root);
		installSkinPaletteCallbacks(_skins);

		root->getOrCreateStateSet()->setAttributeAndModes(
			new osg::CullFace(osg::CullFace::BACK),
			osg::StateAttribute::ON
		);

		return root;
	}

private:
	const tinygltf::Model& _model;
	const osgDB::Options* _readOptions;
	TextureLoader _textureLoader;
	MaterialBuilder _materialBuilder;
	Reader::ProgressCallback _progress;
	std::vector<osg::ref_ptr<osg::Array>> _arrays;
	std::vector<osg::ref_ptr<Skin>> _skins;
	MeshBuilder _meshBuilder;
	std::vector<osg::observer_ptr<osg::MatrixTransform>> _nodeTransforms;
	size_t _nodesBuilt = 0;

	osg::Node* _createNode(int nodeIdx, unsigned depth = 0) {
		if(nodeIdx < 0 || nodeIdx >= static_cast<int>(_model.nodes.size())) return nullptr;

		const size_t nodeIndex = static_cast<size_t>(nodeIdx);
		const tinygltf::Node& node = _model.nodes[nodeIndex];

		GLTF_NOTIFY(depth)
			<< "createNode '" << node.name << "'"
			<< " node=" << nodeIdx
			<< " mesh=" << node.mesh
			<< " skin=" << node.skin
			<< " children=" << node.children.size() << std::endl
		;

		// One tick per node regardless of depth. model.nodes.size() includes
		// unreferenced nodes, so it is an upper-bound denominator.
		_nodesBuilt++;

		if(_progress) _progress(
			Reader::Stage::BuildingNodes,
			_nodesBuilt,
			_model.nodes.size()
		);

		osg::MatrixTransform* transform = new osg::MatrixTransform();

		if(node.matrix.size() == 16) transform->setMatrix(osg::Matrixd(node.matrix.data()));

		if(transform->getMatrix().isIdentity()) {
			osg::Matrixd scale, rotation, translation;

			if(node.scale.size() == 3) scale = osg::Matrixd::scale(
				node.scale[0],
				node.scale[1],
				node.scale[2]
			);

			if(node.rotation.size() == 4) rotation.makeRotate(osg::Quat(
				node.rotation[0],
				node.rotation[1],
				node.rotation[2],
				node.rotation[3]
			));

			if(node.translation.size() == 3) translation = osg::Matrixd::translate(
				node.translation[0],
				node.translation[1],
				node.translation[2]
			);

			transform->setMatrix(scale * rotation * translation);
		}

		_nodeTransforms[nodeIndex] = transform;

		if(node.skin >= 0) {
			const size_t skinIndex = static_cast<size_t>(node.skin);

			if(skinIndex < _skins.size() && _skins[skinIndex].valid()) {
				_skins[skinIndex]->skinnedNodes.push_back(transform);
			}
		}

		if(node.mesh >= 0) {
			const size_t meshIndex = static_cast<size_t>(node.mesh);

			if(meshIndex < _model.meshes.size()) transform->addChild(_meshBuilder.makeMesh(
				_model.meshes[meshIndex],
				node.skin
			));
		}

		for(int childIdx : node.children) {
			if(osg::Node* child = _createNode(childIdx, depth + 1)) {
				transform->addChild(child);
			}
		}

		transform->setName(node.name);

		return transform;
	}

	void _installAnimationCallback(osg::Node* root) const {
		const bool skipAnimation =
			_readOptions &&
			_readOptions->getOptionString().find("gltfSkipAnimation") != std::string::npos
		;

		installAnimationCallback(
			_model,
			_arrays,
			_nodeTransforms,
			root,
			skipAnimation
		);
	}
};

osg::Node* buildScene(
	const tinygltf::Model& model,
	const std::string& referrer,
	const osgDB::Options* readOptions,
	TextureCache* textureCache,
	const Reader::ProgressCallback& progress
) {
	SceneBuilder builder(model, referrer, readOptions, textureCache, progress);

	return builder.build();
}

}
