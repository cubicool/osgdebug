//vimrun! ./test.py

#include "osgx/osgx.hpp"
#include "osgx/Cursor.hpp"
#include "osgx/Debug.hpp"
#include "osgx/ImGui.hpp"
#include "osgx/Linux.hpp"
#include "pyosg/pyosg.hpp"

#ifdef OSGX_EGL
#include "osgx/GraphicsWindowEGL.hpp"
#endif

#ifdef OSGX_GBM
#include "osgx/GraphicsWindowGBM.hpp"
#endif

#ifdef OSGX_GLTF
#include "osgx/gltf/PBRIBL.hpp"
#include "osgx/gltf/Reader.hpp"
#include "osgx/gltf/Shader.hpp"
#include "osgx/gltf/SimplePlayer.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osgDB/FileNameUtils>

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS
#endif

#include "pybind11x.hpp"

#include <pybind11/functional.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace pyx = pybind11x;

#ifdef OSGX_GLTF
namespace {

bool skipImageLoad(
	tinygltf::Image*,
	const int,
	std::string*,
	std::string*,
	int,
	int,
	const unsigned char*,
	int,
	void*
) {
	return true;
}

const char* componentTypeName(int componentType) {
	switch(componentType) {
		case TINYGLTF_COMPONENT_TYPE_BYTE: return "BYTE";
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return "UNSIGNED_BYTE";
		case TINYGLTF_COMPONENT_TYPE_SHORT: return "SHORT";
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return "UNSIGNED_SHORT";
		case TINYGLTF_COMPONENT_TYPE_INT: return "INT";
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return "UNSIGNED_INT";
		case TINYGLTF_COMPONENT_TYPE_FLOAT: return "FLOAT";
		case TINYGLTF_COMPONENT_TYPE_DOUBLE: return "DOUBLE";
		default: return "UNKNOWN";
	}
}

const char* accessorTypeName(int type) {
	switch(type) {
		case TINYGLTF_TYPE_SCALAR: return "SCALAR";
		case TINYGLTF_TYPE_VEC2: return "VEC2";
		case TINYGLTF_TYPE_VEC3: return "VEC3";
		case TINYGLTF_TYPE_VEC4: return "VEC4";
		case TINYGLTF_TYPE_MAT2: return "MAT2";
		case TINYGLTF_TYPE_MAT3: return "MAT3";
		case TINYGLTF_TYPE_MAT4: return "MAT4";
		default: return "UNKNOWN";
	}
}

py::object maybeString(const std::string& value) {
	return value.empty() ? py::none() : py::cast(value);
}

py::object maybeNodeName(const tinygltf::Model& model, int nodeIdx) {
	if(nodeIdx < 0 || nodeIdx >= static_cast<int>(model.nodes.size())) return py::none();

	return maybeString(model.nodes[static_cast<size_t>(nodeIdx)].name);
}

py::dict accessorInfo(const tinygltf::Model& model, int accessorIdx) {
	py::dict out;

	out["index"] = accessorIdx;

	if(accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
		out["valid"] = false;
		return out;
	}

	const tinygltf::Accessor& accessor = model.accessors[static_cast<size_t>(accessorIdx)];

	out["valid"] = true;
	out["name"] = maybeString(accessor.name);
	out["count"] = accessor.count;
	out["componentType"] = componentTypeName(accessor.componentType);
	out["componentTypeValue"] = accessor.componentType;
	out["type"] = accessorTypeName(accessor.type);
	out["typeValue"] = accessor.type;
	out["normalized"] = accessor.normalized;
	out["bufferView"] = accessor.bufferView;
	out["byteOffset"] = accessor.byteOffset;

	py::list minValues;
	for(double value : accessor.minValues) minValues.append(value);
	out["min"] = minValues;

	py::list maxValues;
	for(double value : accessor.maxValues) maxValues.append(value);
	out["max"] = maxValues;

	return out;
}

py::dict textureInfo(const tinygltf::Model& model, int textureIdx, int texCoord) {
	py::dict out;

	out["index"] = textureIdx;
	out["texCoord"] = texCoord;

	if(textureIdx >= 0 && textureIdx < static_cast<int>(model.textures.size())) {
		const tinygltf::Texture& texture = model.textures[static_cast<size_t>(textureIdx)];

		out["valid"] = true;
		out["name"] = maybeString(texture.name);
		out["source"] = texture.source;
		out["sampler"] = texture.sampler;

		if(texture.source >= 0 && texture.source < static_cast<int>(model.images.size())) {
			const tinygltf::Image& image = model.images[static_cast<size_t>(texture.source)];

			out["imageName"] = maybeString(image.name);
			out["imageUri"] = maybeString(image.uri);
			out["imageWidth"] = image.width;
			out["imageHeight"] = image.height;
			out["embedded"] = !image.image.empty();
		}
	}

	else out["valid"] = false;

	return out;
}

py::object textureInfoOrNone(const tinygltf::Model& model, const tinygltf::TextureInfo& info) {
	if(info.index < 0) return py::none();

	return textureInfo(model, info.index, info.texCoord);
}

py::object normalTextureInfoOrNone(
	const tinygltf::Model& model,
	const tinygltf::NormalTextureInfo& info
) {
	if(info.index < 0) return py::none();

	py::dict out = textureInfo(model, info.index, info.texCoord);

	out["scale"] = info.scale;

	return out;
}

py::object occlusionTextureInfoOrNone(
	const tinygltf::Model& model,
	const tinygltf::OcclusionTextureInfo& info
) {
	if(info.index < 0) return py::none();

	py::dict out = textureInfo(model, info.index, info.texCoord);

	out["strength"] = info.strength;

	return out;
}

py::list numberList(const std::vector<double>& values) {
	py::list out;

	for(double value : values) out.append(value);

	return out;
}

py::dict materialInfo(const tinygltf::Model& model, int materialIdx) {
	const tinygltf::Material& material = model.materials[static_cast<size_t>(materialIdx)];
	const tinygltf::PbrMetallicRoughness& pbr = material.pbrMetallicRoughness;

	py::dict out;

	out["index"] = materialIdx;
	out["name"] = maybeString(material.name);
	out["alphaMode"] = material.alphaMode;
	out["alphaCutoff"] = material.alphaCutoff;
	out["doubleSided"] = material.doubleSided;
	out["emissiveFactor"] = numberList(material.emissiveFactor);
	out["normalTexture"] = normalTextureInfoOrNone(model, material.normalTexture);
	out["occlusionTexture"] = occlusionTextureInfoOrNone(model, material.occlusionTexture);
	out["emissiveTexture"] = textureInfoOrNone(model, material.emissiveTexture);

	py::dict pbrDict;

	pbrDict["baseColorFactor"] = numberList(pbr.baseColorFactor);
	pbrDict["metallicFactor"] = pbr.metallicFactor;
	pbrDict["roughnessFactor"] = pbr.roughnessFactor;
	pbrDict["baseColorTexture"] = textureInfoOrNone(model, pbr.baseColorTexture);
	pbrDict["metallicRoughnessTexture"] = textureInfoOrNone(model, pbr.metallicRoughnessTexture);

	out["pbrMetallicRoughness"] = pbrDict;

	bool hasSpecGloss =
		material.extensions.find("KHR_materials_pbrSpecularGlossiness") != material.extensions.end()
	;

	out["hasSpecGloss"] = hasSpecGloss;
	out["requiresSpecGlossBake"] = hasSpecGloss;
	out["hasBaseColorMap"] = pbr.baseColorTexture.index >= 0;
	out["hasMetallicRoughnessMap"] = pbr.metallicRoughnessTexture.index >= 0;
	out["hasNormalMap"] = material.normalTexture.index >= 0;
	out["hasOcclusionMap"] = material.occlusionTexture.index >= 0;
	out["hasEmissiveMap"] = material.emissiveTexture.index >= 0;
	out["factorOnlyPBR"] =
		pbr.baseColorTexture.index < 0 &&
		pbr.metallicRoughnessTexture.index < 0 &&
		!hasSpecGloss
	;
	out["textureDrivenPBR"] =
		pbr.baseColorTexture.index >= 0 ||
		pbr.metallicRoughnessTexture.index >= 0 ||
		hasSpecGloss
	;
	out["usesLowRoughnessFactor"] = pbr.roughnessFactor < 0.35;
	out["likelyReflectiveFromFactors"] =
		pbr.roughnessFactor < 0.35 &&
		pbr.metallicFactor > 0.5
	;

	py::list extensions;

	for(const auto& [name, value] : material.extensions) extensions.append(name);

	out["extensions"] = extensions;

	return out;
}

py::dict primitiveInfo(const tinygltf::Model& model, const tinygltf::Primitive& primitive, int primitiveIdx) {
	py::dict out;
	py::dict attributes;

	out["index"] = primitiveIdx;
	out["mode"] = primitive.mode;
	out["indices"] = accessorInfo(model, primitive.indices);
	out["material"] = primitive.material;

	if(primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
		out["materialName"] = maybeString(
			model.materials[static_cast<size_t>(primitive.material)].name
		);
	}

	for(const auto& [name, accessorIdx] : primitive.attributes) {
		attributes[py::str(name)] = accessorInfo(model, accessorIdx);
	}

	out["attributes"] = attributes;
	out["hasJoints0"] = primitive.attributes.find("JOINTS_0") != primitive.attributes.end();
	out["hasWeights0"] = primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end();
	out["hasPosition"] = primitive.attributes.find("POSITION") != primitive.attributes.end();
	out["hasNormal"] = primitive.attributes.find("NORMAL") != primitive.attributes.end();
	out["hasTangent"] = primitive.attributes.find("TANGENT") != primitive.attributes.end();

	return out;
}

py::dict meshInfo(const tinygltf::Model& model, int meshIdx) {
	const tinygltf::Mesh& mesh = model.meshes[static_cast<size_t>(meshIdx)];
	py::dict out;
	py::list primitives;

	out["index"] = meshIdx;
	out["name"] = maybeString(mesh.name);

	for(size_t i = 0; i < mesh.primitives.size(); i++) {
		primitives.append(primitiveInfo(model, mesh.primitives[i], static_cast<int>(i)));
	}

	out["primitives"] = primitives;
	out["primitiveCount"] = mesh.primitives.size();

	return out;
}

py::dict skinInfo(const tinygltf::Model& model, int skinIdx) {
	const tinygltf::Skin& skin = model.skins[static_cast<size_t>(skinIdx)];
	py::dict out;
	py::list joints;
	py::list users;

	out["index"] = skinIdx;
	out["name"] = maybeString(skin.name);
	out["skeleton"] = skin.skeleton;
	out["skeletonName"] = maybeNodeName(model, skin.skeleton);
	out["inverseBindMatrices"] = accessorInfo(model, skin.inverseBindMatrices);

	for(size_t jointIdx = 0; jointIdx < skin.joints.size(); jointIdx++) {
		int nodeIdx = skin.joints[jointIdx];
		py::dict joint;

		joint["index"] = jointIdx;
		joint["node"] = nodeIdx;
		joint["nodeName"] = maybeNodeName(model, nodeIdx);

		joints.append(joint);
	}

	for(size_t nodeIdx = 0; nodeIdx < model.nodes.size(); nodeIdx++) {
		const tinygltf::Node& node = model.nodes[nodeIdx];

		if(node.skin != skinIdx) continue;

		py::dict user;

		user["node"] = nodeIdx;
		user["nodeName"] = maybeString(node.name);
		user["mesh"] = node.mesh;

		if(node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
			user["meshName"] = maybeString(
				model.meshes[static_cast<size_t>(node.mesh)].name
			);
		}

		users.append(user);
	}

	out["joints"] = joints;
	out["jointCount"] = skin.joints.size();
	out["users"] = users;
	out["userCount"] = py::len(users);

	const auto ibm = model.accessors.size() > static_cast<size_t>(std::max(skin.inverseBindMatrices, 0))
		? skin.inverseBindMatrices
		: -1
	;

	out["inverseBindMatricesMatchJointCount"] =
		ibm >= 0 &&
		model.accessors[static_cast<size_t>(ibm)].count == skin.joints.size()
	;

	return out;
}

py::dict animationInfo(const tinygltf::Model& model, int animationIdx) {
	const tinygltf::Animation& animation =
		model.animations[static_cast<size_t>(animationIdx)];
	py::dict out;
	py::list samplers;
	py::list channels;
	std::set<std::string> paths;
	std::set<std::string> interpolations;
	double duration = 0.0;

	out["index"] = animationIdx;
	out["name"] = maybeString(animation.name);

	for(size_t samplerIdx = 0; samplerIdx < animation.samplers.size(); samplerIdx++) {
		const tinygltf::AnimationSampler& sampler = animation.samplers[samplerIdx];
		py::dict item;

		item["index"] = samplerIdx;
		item["input"] = accessorInfo(model, sampler.input);
		item["output"] = accessorInfo(model, sampler.output);
		item["interpolation"] = sampler.interpolation.empty() ? "LINEAR" : sampler.interpolation;

		if(
			sampler.input >= 0 &&
			sampler.input < static_cast<int>(model.accessors.size()) &&
			!model.accessors[static_cast<size_t>(sampler.input)].maxValues.empty()
		) {
			const tinygltf::Accessor& input =
				model.accessors[static_cast<size_t>(sampler.input)];

			duration = std::max(duration, input.maxValues[0]);
			item["endTime"] = input.maxValues[0];
		}

		interpolations.insert(sampler.interpolation.empty() ? "LINEAR" : sampler.interpolation);
		samplers.append(item);
	}

	for(size_t channelIdx = 0; channelIdx < animation.channels.size(); channelIdx++) {
		const tinygltf::AnimationChannel& channel = animation.channels[channelIdx];
		py::dict item;

		item["index"] = channelIdx;
		item["sampler"] = channel.sampler;
		item["targetNode"] = channel.target_node;
		item["targetNodeName"] = maybeNodeName(model, channel.target_node);
		item["targetPath"] = channel.target_path;
		item["supportedByCurrentLoader"] =
			channel.target_path == "translation" ||
			channel.target_path == "rotation" ||
			channel.target_path == "scale"
		;

		paths.insert(channel.target_path);
		channels.append(item);
	}

	py::list pathList;
	for(const auto& path : paths) pathList.append(path);

	py::list interpolationList;
	for(const auto& interpolation : interpolations) interpolationList.append(interpolation);

	out["samplers"] = samplers;
	out["samplerCount"] = animation.samplers.size();
	out["channels"] = channels;
	out["channelCount"] = animation.channels.size();
	out["targetPaths"] = pathList;
	out["interpolations"] = interpolationList;
	out["duration"] = duration;
	out["hasMorphTargetAnimation"] = paths.find("weights") != paths.end();
	out["hasCubicSpline"] = interpolations.find("CUBICSPLINE") != interpolations.end();

	return out;
}

py::dict inspectGLTF(const std::string& path, bool loadImages) {
	std::string err;
	std::string warn;
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	const std::string ext = osgDB::getLowerCaseFileExtension(path);

	if(!loadImages) loader.SetImageLoader(skipImageLoad, nullptr);

	bool ok = ext == "glb"
		? loader.LoadBinaryFromFile(&model, &err, &warn, path)
		: loader.LoadASCIIFromFile(&model, &err, &warn, path)
	;

	if(!ok || !err.empty()) {
		throw std::runtime_error("failed to load " + path + ": " + err);
	}

	py::dict out;
	py::dict asset;
	py::dict counts;
	py::dict intent;
	py::list warnings;
	py::list scenes;
	py::list nodes;
	py::list meshes;
	py::list materials;
	py::list skins;
	py::list animations;

	if(!warn.empty()) warnings.append(warn);

	asset["version"] = model.asset.version;
	asset["minVersion"] = maybeString(model.asset.minVersion);
	asset["generator"] = maybeString(model.asset.generator);
	asset["copyright"] = maybeString(model.asset.copyright);

	counts["scenes"] = model.scenes.size();
	counts["nodes"] = model.nodes.size();
	counts["meshes"] = model.meshes.size();
	counts["materials"] = model.materials.size();
	counts["textures"] = model.textures.size();
	counts["images"] = model.images.size();
	counts["skins"] = model.skins.size();
	counts["animations"] = model.animations.size();
	counts["accessors"] = model.accessors.size();
	counts["bufferViews"] = model.bufferViews.size();
	counts["buffers"] = model.buffers.size();

	for(size_t sceneIdx = 0; sceneIdx < model.scenes.size(); sceneIdx++) {
		const tinygltf::Scene& scene = model.scenes[sceneIdx];
		py::dict sceneDict;
		py::list sceneNodes;

		sceneDict["index"] = sceneIdx;
		sceneDict["name"] = maybeString(scene.name);

		for(int nodeIdx : scene.nodes) sceneNodes.append(nodeIdx);

		sceneDict["nodes"] = sceneNodes;

		scenes.append(sceneDict);
	}

	for(size_t nodeIdx = 0; nodeIdx < model.nodes.size(); nodeIdx++) {
		const tinygltf::Node& node = model.nodes[nodeIdx];
		py::dict nodeDict;
		py::list children;

		nodeDict["index"] = nodeIdx;
		nodeDict["name"] = maybeString(node.name);
		nodeDict["mesh"] = node.mesh;
		nodeDict["skin"] = node.skin;

		for(int childIdx : node.children) children.append(childIdx);

		nodeDict["children"] = children;

		nodeDict["hasMatrix"] = node.matrix.size() == 16;
		nodeDict["hasTranslation"] = node.translation.size() == 3;
		nodeDict["hasRotation"] = node.rotation.size() == 4;
		nodeDict["hasScale"] = node.scale.size() == 3;

		nodes.append(nodeDict);
	}

	for(size_t meshIdx = 0; meshIdx < model.meshes.size(); meshIdx++) {
		meshes.append(meshInfo(model, static_cast<int>(meshIdx)));
	}

	for(size_t materialIdx = 0; materialIdx < model.materials.size(); materialIdx++) {
		materials.append(materialInfo(model, static_cast<int>(materialIdx)));
	}

	for(size_t skinIdx = 0; skinIdx < model.skins.size(); skinIdx++) {
		skins.append(skinInfo(model, static_cast<int>(skinIdx)));
	}

	for(size_t animationIdx = 0; animationIdx < model.animations.size(); animationIdx++) {
		animations.append(animationInfo(model, static_cast<int>(animationIdx)));
	}

	bool hasSpecGloss = false;
	bool hasJoints0 = false;
	bool hasWeights0 = false;
	bool hasMorphTargets = false;
	bool hasPBRTextures = false;

	for(const auto& material : model.materials) {
		hasSpecGloss = hasSpecGloss ||
			material.extensions.find("KHR_materials_pbrSpecularGlossiness") != material.extensions.end()
		;

		hasPBRTextures = hasPBRTextures ||
			material.pbrMetallicRoughness.baseColorTexture.index >= 0 ||
			material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0 ||
			material.normalTexture.index >= 0 ||
			material.occlusionTexture.index >= 0 ||
			material.emissiveTexture.index >= 0
		;
	}

	for(const auto& mesh : model.meshes) {
		for(const auto& primitive : mesh.primitives) {
			hasJoints0 = hasJoints0 || primitive.attributes.find("JOINTS_0") != primitive.attributes.end();
			hasWeights0 = hasWeights0 || primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end();
			hasMorphTargets = hasMorphTargets || !primitive.targets.empty();
		}
	}

	intent["hasSkinning"] = !model.skins.empty() || hasJoints0 || hasWeights0;
	intent["hasAnimation"] = !model.animations.empty();
	intent["hasMorphTargets"] = hasMorphTargets;
	intent["hasSpecGloss"] = hasSpecGloss;
	intent["hasPBRTextures"] = hasPBRTextures;
	intent["hasJoints0"] = hasJoints0;
	intent["hasWeights0"] = hasWeights0;

	out["path"] = path;
	out["asset"] = asset;
	out["counts"] = counts;
	out["intent"] = intent;
	out["warnings"] = warnings;
	out["defaultScene"] = model.defaultScene;
	out["scenes"] = scenes;
	out["nodes"] = nodes;
	out["meshes"] = meshes;
	out["materials"] = materials;
	out["skins"] = skins;
	out["animations"] = animations;

	return out;
}

std::string inspectGLTFJson(const std::string& path, bool loadImages, int indent) {
	py::object json = py::module_::import("json");

	return py::str(json.attr("dumps")(
		inspectGLTF(path, loadImages),
		"indent"_a=indent,
		"sort_keys"_a=true
	));
}

// Async glTF load: releases the GIL and calls osgx::gltf::Reader directly (bypassing the generic
// osgDB::readNodeFile plugin dispatch, which has no hook for a progress callback), so a
// caller can run this via asyncio.to_thread(...) while the viewer keeps rendering. Progress
// (and the final node) are delivered through the same loop/queue call_soon_threadsafe bridge
// as pyosg_async_task_example -- see pybind11x::put_nowait.
//
// Cancellation via `stop` is cooperative and can only take effect between osgx::gltf::Reader's own
// checkpoints (see osgx::gltf::Reader::ProgressCallback) -- it cannot interrupt tinygltf's own file
// parse/decode, which is a single opaque blocking call. If a stop was requested by the time
// read() returns, the result is discarded (not attached to the scene) and "complete" is
// delivered with None instead of the loaded node.
osg::ref_ptr<osg::Node> readNodeFileAsync(
	std::string location,
	pyx::StopEvent* stop,
	py::object loop,
	py::object queue,
	size_t job_id
) {
	py::gil_scoped_release release;

	// Same texture-dedup cache ReaderWriterGLTF::readNode() wires up for the normal
	// osgDB::readNodeFile() path. Without this, the reader's three cache-checking call sites
	// (occlusion/metallic-roughness bake, base color, normal map) all skip their cache lookup and
	// silently reload+redecode any texture referenced by more than one material in the
	// model -- measured 4.5x slower (13.5s vs 2.96s) on a real multi-material asset
	// before this was added.
	static osgx::gltf::Reader::TextureCache s_asyncTextureCache;

	const std::string ext = osgDB::getLowerCaseFileExtension(location);
	const bool isBinary = ext == "glb";

	osgx::gltf::Reader reader;

	reader.setTextureCache(&s_asyncTextureCache);

	osgx::gltf::Reader::ProgressCallback onProgress = [&](
		osgx::gltf::Reader::Stage stage,
		size_t current,
		size_t total
	) {
		pyx::put_nowait(
			loop,
			queue,
			"progress",
			job_id,
			std::string(osgx::gltf::Reader::stageName(stage)),
			current,
			total
		);
	};

	auto result = reader.read(location, isBinary, nullptr, onProgress);

	if(stop && stop->stop.load(std::memory_order_relaxed)) {
		pyx::put_nowait(loop, queue, "complete", job_id, py::none());

		return nullptr;
	}

	osg::ref_ptr<osg::Node> node = result.validNode() ? result.getNode() : nullptr;

	pyx::put_nowait(loop, queue, "complete", job_id, node);

	return node;
}

}
#endif

PYBIND11_MODULE(osgx, m) {
	auto py_osg = py::module_::import("OpenSceneGraph");

	osgx::pbr::registerShaderLibs();
	osgx::ibl::registerShaderLibs();

	auto grid = py::class_<
		osgx::Grid,
		osg::Geometry,
		osg::ref_ptr<osgx::Grid>
	>(m, "Grid");

	py::enum_<osgx::Grid::EdgeMode>(grid, "EdgeMode")
		.value("EDGE_ASIS", osgx::Grid::EDGE_ASIS)
		.value("EDGE_HIDE", osgx::Grid::EDGE_HIDE)
		.value("EDGE_NUDGE", osgx::Grid::EDGE_NUDGE)
		.export_values()
	;

	py::enum_<osgx::Grid::LineMode>(grid, "LineMode")
		.value("LINE_SCREEN_PIXELS", osgx::Grid::LINE_SCREEN_PIXELS)
		.value("LINE_GRID_UNITS", osgx::Grid::LINE_GRID_UNITS)
		.export_values()
	;

	grid
		.def(py::init<>())
		.def(py::init<const osg::Vec3&, const osg::Vec3&, const osg::Vec3&>(),
			"corner"_a, "width_vec"_a, "height_vec"_a
		)
		.def_property("canvasSize", &osgx::Grid::getCanvasSize, &osgx::Grid::setCanvasSize)
		.def_property("gridInterval", &osgx::Grid::getGridInterval, &osgx::Grid::setGridInterval)
		.def_property(
			"gridIntervalStrong",
			&osgx::Grid::getGridIntervalStrong,
			&osgx::Grid::setGridIntervalStrong
		)
		.def_property("lineWidthPx", &osgx::Grid::getLineWidthPx, &osgx::Grid::setLineWidthPx)
		.def_property("lineWidth", &osgx::Grid::getLineWidth, &osgx::Grid::setLineWidth)
		.def_property("edgeMode", &osgx::Grid::getEdgeMode, &osgx::Grid::setEdgeMode)
		.def_property("lineMode", &osgx::Grid::getLineMode, &osgx::Grid::setLineMode)
		.def_property("colorBg", &osgx::Grid::getColorBg, &osgx::Grid::setColorBg)
		.def_property("colorLine", &osgx::Grid::getColorLine, &osgx::Grid::setColorLine)
		.def_property(
			"colorLineStrong",
			&osgx::Grid::getColorLineStrong,
			&osgx::Grid::setColorLineStrong
		)
		.def("orthoCamera", &osgx::Grid::orthoCamera)
		.def_static("createOrthoCamera", py::overload_cast<>(&osgx::Grid::createOrthoCamera))
		.def_static(
			"createOrthoCamera",
			py::overload_cast<const osg::Vec3&, const osg::Vec3&, const osg::Vec3&>(
				&osgx::Grid::createOrthoCamera
			),
			"corner"_a, "width_vec"_a, "height_vec"_a
		)
	;

	// osgx::Ortho2DManipulator / OrbitAxisManipulator / MultiCameraManipulator (osgx/Manipulators.hpp).
	// All three derive from osgGA::CameraManipulator, already registered by pyosgGA.cpp as
	// "CameraManipulator" -- its node/matrix/inverseMatrix/homePosition properties and
	// home()/init()/handle() methods come along automatically via virtual dispatch, so only each
	// subclass's OWN new surface needs binding here.
	py::class_<
		osgx::Ortho2DManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::Ortho2DManipulator>
	>(m, "Ortho2DManipulator")
		.def(py::init<>())
		.def_property(
			"pixelNudge",
			&osgx::Ortho2DManipulator::getPixelNudge,
			&osgx::Ortho2DManipulator::setPixelNudge
		)
		.def_property(
			"wheelZoomFactor",
			&osgx::Ortho2DManipulator::getWheelZoomFactor,
			&osgx::Ortho2DManipulator::setWheelZoomFactor
		)
		.def_property(
			"rotateSensitivity",
			&osgx::Ortho2DManipulator::getRotateSensitivity,
			&osgx::Ortho2DManipulator::setRotateSensitivity
		)
		.def_property(
			"invertY",
			&osgx::Ortho2DManipulator::getInvertY,
			&osgx::Ortho2DManipulator::setInvertY,
			"Inverts the Y axis for Ctrl-drag 3D pitch only; plain pan is unaffected."
		)
		.def_property(
			"invertX",
			&osgx::Ortho2DManipulator::getInvertX,
			&osgx::Ortho2DManipulator::setInvertX,
			"Inverts the X axis for Ctrl-drag 3D yaw only; plain pan is unaffected."
		)
		.def_property(
			"planeNormal",
			&osgx::Ortho2DManipulator::getPlaneNormal,
			&osgx::Ortho2DManipulator::setPlaneNormal,
			"Normal of the unrotated 2D plane (default: +Z)."
		)
		.def_property(
			"screenUp",
			&osgx::Ortho2DManipulator::getScreenUp,
			&osgx::Ortho2DManipulator::setScreenUp,
			"Up direction of the unrotated 2D plane (default: +Y)."
		)
		.def_property(
			"center",
			&osgx::Ortho2DManipulator::getCenter,
			&osgx::Ortho2DManipulator::setCenter
		)
		.def_property(
			"halfExtentY",
			&osgx::Ortho2DManipulator::getHalfExtentY,
			&osgx::Ortho2DManipulator::setHalfExtentY
		)
		.def_property(
			"zoomLimits",
			&osgx::Ortho2DManipulator::getZoomLimits,
			[](osgx::Ortho2DManipulator& self, py::object obj) {
				auto vals = pyx::try_unpack_sequence<double, double>(obj);

				if(!vals) throw py::type_error(
					"Expected a (minHalfExtent, maxHalfExtent) sequence of length 2"
				);

				auto& [minH, maxH] = *vals;

				self.setZoomLimits(minH, maxH);
			},
			"(minHalfExtent, maxHalfExtent) clamp for halfExtentY."
		)
	;

	py::class_<
		osgx::OrbitAxisManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::OrbitAxisManipulator>
	>(m, "OrbitAxisManipulator")
		.def(py::init<>())
		.def_property(
			"yawSensitivity",
			&osgx::OrbitAxisManipulator::getYawSensitivity,
			&osgx::OrbitAxisManipulator::setYawSensitivity
		)
		.def_property(
			"heightSensitivity",
			&osgx::OrbitAxisManipulator::getHeightSensitivity,
			&osgx::OrbitAxisManipulator::setHeightSensitivity
		)
		.def_property(
			"wheelZoomFactor",
			&osgx::OrbitAxisManipulator::getWheelZoomFactor,
			&osgx::OrbitAxisManipulator::setWheelZoomFactor
		)
		.def_property(
			"invertY",
			&osgx::OrbitAxisManipulator::getInvertY,
			&osgx::OrbitAxisManipulator::setInvertY,
			"Inverts the pointer Y axis used for axial motion, in both the raw MOVE/DRAG path and orbitByDelta()."
		)
		.def_property(
			"upAxis",
			&osgx::OrbitAxisManipulator::getUpAxis,
			&osgx::OrbitAxisManipulator::setUpAxis,
			"Turntable up axis (default: +Z)."
		)
		.def_property(
			"homeDirection",
			&osgx::OrbitAxisManipulator::getHomeDirection,
			&osgx::OrbitAxisManipulator::setHomeDirection,
			"Horizontal camera direction at yaw == 0 (default: -Y)."
		)
		.def_property(
			"coverageLimits",
			&osgx::OrbitAxisManipulator::getCoverageLimits,
			[](osgx::OrbitAxisManipulator& self, py::object obj) {
				auto vals = pyx::try_unpack_sequence<double, double>(obj);

				if(!vals) throw py::type_error(
					"Expected a (minCoverage, maxCoverage) sequence of length 2"
				);

				auto& [minC, maxC] = *vals;

				self.setCoverageLimits(minC, maxC);
			},
			"(minCoverage, maxCoverage) viewport-coverage fractions at the zoom extremes."
		)
		.def_property(
			"liveOrbitEnabled",
			&osgx::OrbitAxisManipulator::isLiveOrbitEnabled,
			&osgx::OrbitAxisManipulator::setLiveOrbitEnabled,
			"Disable to drive orbit/height exclusively via orbitByDelta() (e.g. from "
			"osgx.platform.PointerCapture) instead of raw MOVE/DRAG cursor tracking."
		)
		.def_property_readonly("yaw", &osgx::OrbitAxisManipulator::getYaw)
		.def_property_readonly("height", &osgx::OrbitAxisManipulator::getHeight)
		.def_property_readonly("distance", &osgx::OrbitAxisManipulator::getDistance)
		.def(
			"orbitByDelta",
			&osgx::OrbitAxisManipulator::orbitByDelta,
			"dx"_a,
			"dy"_a,
			"Applies a pre-computed (dx, dy) directly, in the same normalized units as "
			"GUIEventAdapter.xNormalized/yNormalized."
		)
	;

	py::class_<
		osgx::MultiCameraManipulator,
		osgGA::CameraManipulator,
		osg::ref_ptr<osgx::MultiCameraManipulator>
	>(m, "MultiCameraManipulator")
		.def(py::init<>())
		.def_property(
			"toggleKey",
			&osgx::MultiCameraManipulator::getToggleKey,
			&osgx::MultiCameraManipulator::setToggleKey
		)
		.def_property_readonly("activeIndex", &osgx::MultiCameraManipulator::getActiveIndex)
		.def_property_readonly("numTargets", &osgx::MultiCameraManipulator::getNumTargets)
		.def(
			"addTarget",
			&osgx::MultiCameraManipulator::addTarget,
			"name"_a,
			"manipulator"_a,
			"camera"_a=nullptr,
			"scene"_a=nullptr,
			"setActive"_a=std::function<void(bool)>()
		)
		.def("activate", &osgx::MultiCameraManipulator::activate, "index"_a)
		.def("next", &osgx::MultiCameraManipulator::next)
	;

	// osgx::pbr / osgx::ibl - ported from the STATIC path of pyosg-lighting/09-ibl.py and
	// already proven in osgSlug's osgslug-pbr-ibl.cpp; the goal is for Python demos to reuse
	// this toolkit (GLSL snippets + resolveShaderLibs() + the cubemap/BRDF-LUT/SH9 host-side
	// helpers) instead of re-deriving the shader/UBO plumbing from scratch each time.
	m.def(
		"resolveShaderLibs",
		&osgx::resolveShaderLibs,
		"src"_a,
		"Expand '#pragma osgx::pbr ...' / '#pragma osgx::ibl ...' lines into their GLSL source."
	);

	auto m_pbr = m.def_submodule("pbr", "osgx::pbr - BRDF math GLSL snippets + direct-light rig");

	m_pbr.attr("D_GGX") = osgx::pbr::D_GGX;
	m_pbr.attr("G_SCHLICK") = osgx::pbr::G_SCHLICK;
	m_pbr.attr("G_SMITH") = osgx::pbr::G_SMITH;
	m_pbr.attr("F_SCHLICK") = osgx::pbr::F_SCHLICK;
	m_pbr.attr("F_SCHLICK_ROUGHNESS") = osgx::pbr::F_SCHLICK_ROUGHNESS;
	m_pbr.attr("DIRECT_SPECULAR") = osgx::pbr::DIRECT_SPECULAR;
	m_pbr.attr("F_MULTISCATTER") = osgx::pbr::F_MULTISCATTER;
	m_pbr.attr("IBL_SPECULAR") = osgx::pbr::IBL_SPECULAR;
	m_pbr.attr("TONEMAP_PBR_NEUTRAL") = osgx::pbr::TONEMAP_PBR_NEUTRAL;

	m_pbr.def("snippets", &osgx::pbr::snippets);

	py::class_<osgx::pbr::OrbitLightRig::Orbit>(m_pbr, "Orbit")
		.def(
			py::init([](float radius, float height, float speed, float phase, float intensity) {
				return osgx::pbr::OrbitLightRig::Orbit{radius, height, speed, phase, intensity};
			}),
			"radius"_a=0.5f,
			"height"_a=0.5f,
			"speed"_a=0.5f,
			"phase"_a=0.0f,
			"intensity"_a=1.0f
		)
		.def_readwrite("radius", &osgx::pbr::OrbitLightRig::Orbit::radius)
		.def_readwrite("height", &osgx::pbr::OrbitLightRig::Orbit::height)
		.def_readwrite("speed", &osgx::pbr::OrbitLightRig::Orbit::speed)
		.def_readwrite("phase", &osgx::pbr::OrbitLightRig::Orbit::phase)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::Orbit::intensity)
	;

	py::class_<
		osgx::pbr::OrbitLightRig,
		osg::NodeCallback,
		osg::ref_ptr<osgx::pbr::OrbitLightRig>
	>(m_pbr, "OrbitLightRig")
		.def(py::init<>())
		.def_readwrite("ss", &osgx::pbr::OrbitLightRig::ss)
		.def_readwrite("center", &osgx::pbr::OrbitLightRig::center)
		.def_readwrite("intensity", &osgx::pbr::OrbitLightRig::intensity)
		.def_readwrite("uniformName", &osgx::pbr::OrbitLightRig::uniformName)
		.def_readwrite("orbits", &osgx::pbr::OrbitLightRig::orbits)
	;

	auto m_ibl = m.def_submodule(
		"ibl",
		"osgx::ibl - prefiltered cubemap + BRDF LUT + SH9/baked-Lambertian diffuse"
	);

	m_ibl.attr("FULLSCREEN_VERT") = osgx::ibl::FULLSCREEN_VERT;
	m_ibl.attr("BRDF_LUT_FRAG") = osgx::ibl::BRDF_LUT_FRAG;
	m_ibl.attr("SH_IRRADIANCE") = osgx::ibl::SH_IRRADIANCE;
	m_ibl.attr("LAMBERTIAN_IRRADIANCE") = osgx::ibl::LAMBERTIAN_IRRADIANCE;

	py::class_<
		osgx::ibl::RunOnceCallback,
		osg::NodeCallback,
		osg::ref_ptr<osgx::ibl::RunOnceCallback>
	>(m_ibl, "RunOnceCallback")
		.def(py::init<>())
		.def("rebake", &osgx::ibl::RunOnceCallback::rebake, "node"_a)
	;

	m_ibl
		.def(
			"loadPrefilterCubemap",
			&osgx::ibl::loadPrefilterCubemap,
			"path"_a,
			"Loads a pre-baked GGX-prefiltered cubemap (.ktx2); returns None (and logs OSG_WARN) "
			"if the path doesn't load as a TextureCubeMap."
		)
		.def(
			"makeBRDFLUTCamera",
			&osgx::ibl::makeBRDFLUTCamera,
			"lutSize"_a,
			"lut"_a,
			"Configures `lut` in-place (size/format/filters) and returns a PRE_RENDER camera that "
			"bakes the split-sum BRDF LUT into it exactly once (see RunOnceCallback)."
		)
	;

	py::class_<osgx::ibl::SH9>(m_ibl, "SH9")
		.def(py::init<>())
		.def("__len__", [](const osgx::ibl::SH9&) { return 9; })
		.def("__getitem__", [](const osgx::ibl::SH9& self, size_t i) {
			if(i >= 9) throw py::index_error();

			return self.coeffs[i];
		})
		.def("__setitem__", [](osgx::ibl::SH9& self, size_t i, const osg::Vec3f& v) {
			if(i >= 9) throw py::index_error();

			self.coeffs[i] = v;
		})
	;

	m_ibl.def(
		"computeSH",
		&osgx::ibl::computeSH,
		"image"_a,
		"Projects an equirectangular HDR/LDR osg.Image onto SH9 diffuse irradiance coefficients."
	);

	m_ibl.def(
		"computeLambertianCubeMap",
		&osgx::ibl::computeLambertianCubeMap,
		"image"_a,
		"size"_a = 64,
		"samples"_a = 256,
		py::call_guard<py::gil_scoped_release>(),
		"Bakes a cosine-weighted Monte Carlo diffuse irradiance cubemap from an equirectangular "
		"HDR/LDR osg.Image -- more accurate than SH9 (see computeSH), at the cost of a real bake "
		"instead of 9 coefficients. Sample with LAMBERTIAN_IRRADIANCE's osgx_LambertianIrradiance()."
	);

	py::class_<osgx::ibl::GGXPrefilterOptions>(m_ibl, "GGXPrefilterOptions")
		.def(py::init<>())
		.def_readwrite("prefilterSize", &osgx::ibl::GGXPrefilterOptions::prefilterSize)
		.def_readwrite("maxFrames", &osgx::ibl::GGXPrefilterOptions::maxFrames)
		.def_readwrite("readbackFrame", &osgx::ibl::GGXPrefilterOptions::readbackFrame)
		.def_readwrite("syncReadback", &osgx::ibl::GGXPrefilterOptions::syncReadback)
	;

	py::class_<
		osgx::ibl::GGXPrefilterReadback,
		osg::Camera::DrawCallback,
		osg::ref_ptr<osgx::ibl::GGXPrefilterReadback>
	>(m_ibl, "GGXPrefilterReadback")
		.def_property_readonly("done", &osgx::ibl::GGXPrefilterReadback::isDone)
		.def_property_readonly("result", &osgx::ibl::GGXPrefilterReadback::getResult)
		.def("reset", &osgx::ibl::GGXPrefilterReadback::reset)
	;

	py::class_<osgx::ibl::GGXPrefilterScene>(m_ibl, "GGXPrefilterScene")
		.def_readonly("root", &osgx::ibl::GGXPrefilterScene::root)
		.def_readonly("readback", &osgx::ibl::GGXPrefilterScene::readback)
	;

	m_ibl
		.def(
			"createGGXPrefilterScene",
			&osgx::ibl::createGGXPrefilterScene,
			"equirectImage"_a,
			"options"_a = osgx::ibl::GGXPrefilterOptions()
		)
		.def(
			"rebakeGGXPrefilterScene",
			&osgx::ibl::rebakeGGXPrefilterScene,
			"scene"_a,
			"equirectImage"_a
		)
		.def("finishGGXPrefilter", &osgx::ibl::finishGGXPrefilter, "readback"_a)
	;

	// osgx::debug -- GL_KHR_debug integration (push/pop debug groups, message
	// inserts) plus the two-phase per-drawable GPU/CPU profiler.
	auto m_debug = m.def_submodule("debug", "osgx::debug - GL_KHR_debug integration + GPU/CPU profiler");

	py::class_<
		osgx::debug::GraphicsOperation,
		osg::GraphicsOperation,
		osg::ref_ptr<osgx::debug::GraphicsOperation>
	>(m_debug, "GraphicsOperation")
		.def(py::init<>())
	;

	py::enum_<osgx::debug::Severity>(m_debug, "Severity")
		.value("DONT_CARE", osgx::debug::Severity::DONT_CARE)
		.value("HIGH", osgx::debug::Severity::HIGH)
		.value("MEDIUM", osgx::debug::Severity::MEDIUM)
		.value("LOW", osgx::debug::Severity::LOW)
		.value("NOTIFICATION", osgx::debug::Severity::NOTIFICATION)
		.export_values()
	;

	py::enum_<osgx::debug::Source>(m_debug, "Source")
		.value("DONT_CARE", osgx::debug::Source::DONT_CARE)
		.value("API", osgx::debug::Source::API)
		.value("WINDOW_SYSTEM", osgx::debug::Source::WINDOW_SYSTEM)
		.value("SHADER_COMPILER", osgx::debug::Source::SHADER_COMPILER)
		.value("THIRD_PARTY", osgx::debug::Source::THIRD_PARTY)
		.value("APPLICATION", osgx::debug::Source::APPLICATION)
		.value("OTHER", osgx::debug::Source::OTHER)
		.export_values()
	;

	py::enum_<osgx::debug::Type>(m_debug, "Type")
		.value("DONT_CARE", osgx::debug::Type::DONT_CARE)
		.value("ERROR", osgx::debug::Type::ERROR)
		.value("DEPRECATED_BEHAVIOR", osgx::debug::Type::DEPRECATED_BEHAVIOR)
		.value("UNDEFINED_BEHAVIOR", osgx::debug::Type::UNDEFINED_BEHAVIOR)
		.value("PORTABILITY", osgx::debug::Type::PORTABILITY)
		.value("PERFORMANCE", osgx::debug::Type::PERFORMANCE)
		.value("OTHER", osgx::debug::Type::OTHER)
		.value("MARKER", osgx::debug::Type::MARKER)
		.value("PUSH_GROUP", osgx::debug::Type::PUSH_GROUP)
		.value("POP_GROUP", osgx::debug::Type::POP_GROUP)
		.export_values()
	;

	m_debug
		.def("initialize", &osgx::debug::initialize)
		.def(
			"deinitialize",
			&osgx::debug::deinitialize,
			"disableOutput"_a=true
		)
		.def(
			"installDefaultCallback",
			&osgx::debug::installDefaultCallback,
			"synchronous"_a=true
		)
		.def("clearCallback", &osgx::debug::clearCallback)
		.def(
			"enableDebugOutput",
			&osgx::debug::enableDebugOutput,
			"synchronous"_a=true
		)
		.def("disableDebugOutput", &osgx::debug::disableDebugOutput)
		.def(
			"messageControl",
			py::overload_cast<osgx::debug::Source, osgx::debug::Type, osgx::debug::Severity, bool>(
				&osgx::debug::messageControl
			),
			"source"_a,
			"type"_a,
			"severity"_a,
			"enabled"_a
		)
		.def("messageInsert", py::overload_cast<
			osgx::debug::Source,
			osgx::debug::Type,
			GLuint,
			osgx::debug::Severity,
			const std::string&
		>(&osgx::debug::messageInsert),
			"source"_a,
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def("messageInsert", py::overload_cast<
			osgx::debug::Type,
			GLuint,
			osgx::debug::Severity,
			const std::string&
		>(&osgx::debug::messageInsert),
			"type"_a,
			"id"_a,
			"severity"_a,
			"message"_a
		)
		.def(
			"pushGroup",
			py::overload_cast<osgx::debug::Source, GLuint, const std::string&>(&osgx::debug::pushGroup)
		)
		.def(
			"pushGroup",
			py::overload_cast<GLuint, const std::string&>(&osgx::debug::pushGroup)
		)
		.def("popGroup", &osgx::debug::popGroup)
	;

	py::class_<osgx::debug::Scoped>(m_debug, "Scoped")
		.def(
			py::init<GLuint, std::string_view, osgx::debug::Source, bool>(),
			py::arg("id"),
			py::arg("message"),
			py::arg("source")=osgx::debug::Source::APPLICATION,
			py::arg("measureTime")=false
		)
		.def("__enter__", [](osgx::debug::Scoped& self) -> osgx::debug::Scoped& {
			return self;
		})
		.def("__exit__", [](
			osgx::debug::Scoped& self,
			py::object exc_type,
			py::object exc_value,
			py::object traceback
		) {
			return false; // don't suppress Python exceptions
		})
	;

	// osgx::imgui -- deliberately NOT a general ImGui wrapper (that's pyimgui's
	// job elsewhere). Just enough to build quick debugging knobs inside a
	// Widget::addSection() callback: a handful of stateless functions returning
	// (changed, value) tuples, since Python floats/bools aren't mutable
	// references the way ImGui's C++ &value out-params expect. A sibling of
	// osgx::debug, not nested under it -- most of this (Panel/Widget/sliders) has
	// nothing to do with the profiler; only ProfilerSection reaches into debug::.
#ifdef OSGX_IMGUI
	auto m_imgui = m.def_submodule("imgui", "osgx::imgui namespace");

	py::enum_<osgx::imgui::Dock>(m_imgui, "Dock")
		.value("NONE", osgx::imgui::Dock::NONE)
		.value("LEFT", osgx::imgui::Dock::LEFT)
		.value("RIGHT", osgx::imgui::Dock::RIGHT)
		.export_values()
	;

	py::class_<osgx::imgui::Options>(m_imgui, "Options")
		.def(py::init<>())
		.def_readwrite("show_gpu_info", &osgx::imgui::Options::showGPUInfo)
		.def_readwrite("show_frame_info", &osgx::imgui::Options::showFrameInfo)
		.def_readwrite("dock", &osgx::imgui::Options::dock)
		.def_readwrite("dock_width", &osgx::imgui::Options::dockWidth)
	;

	// A growable options bag for addSection() -- expected to grow (a size hint
	// beyond expand/constrain, tooltips, etc.), so this is keyword-constructible
	// from Python rather than adding more positional args to addSection itself.
	py::class_<osgx::imgui::SectionOptions>(m_imgui, "SectionOptions")
		.def(
			py::init(&osgx::imgui::makeSectionOptions),
			"expand"_a=false,
			"default_open"_a=false
		)
		.def_readwrite("expand", &osgx::imgui::SectionOptions::expand)
		.def_readwrite("default_open", &osgx::imgui::SectionOptions::defaultOpen)
	;

	py::class_<osgx::imgui::Panel>(m_imgui, "Panel")
		.def(py::init<>())
		.def(
			"addSection", &osgx::imgui::Panel::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgx::imgui::SectionOptions()
		)
		.def("removeSection", &osgx::imgui::Panel::removeSection, "label"_a)
		.def("clearSections", &osgx::imgui::Panel::clearSections)
		.def(
			"addStatsSection",
			&osgx::imgui::Panel::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgx::imgui::Panel::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false,
			"print_every"_a=0
		)
		.def(
			"addTextureSection",
			py::overload_cast<
				osgViewer::View&,
				osg::Node*, bool
			>(&osgx::imgui::Panel::addTextureSection),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgx::imgui::Panel::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
		.def("draw", &osgx::imgui::Panel::draw, "render_info"_a)
	;

	py::class_<
		osgx::imgui::Widget,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgx::imgui::Widget>
	>(m_imgui, "Widget")
		.def(
			// osgViewer::View, not the full Viewer -- Widget only needs getCamera()
			// (cached once) and getEventHandlers(); a Python osgViewer::Viewer still
			// works here since it upcasts (View is separately registered above and
			// Viewer's py::class_ lists it as a base).
			py::init<osgViewer::View&, osg::Camera*, osgx::imgui::Options>(),
			"viewer"_a,
			"draw_camera"_a=nullptr,
			"options"_a=osgx::imgui::Options()
		)
		.def(
			"addSection",
			&osgx::imgui::Widget::addSection,
			"label"_a,
			"fn"_a,
			"options"_a=osgx::imgui::SectionOptions()
		)
		.def("removeSection", &osgx::imgui::Widget::removeSection, "label"_a)
		.def("clearSections", &osgx::imgui::Widget::clearSections)
		// Widget no longer holds a Viewer/View reference of its own (see the C++
		// class comment), so these now take one explicitly instead of reusing
		// whatever Widget was constructed with.
		.def(
			"addStatsSection",
			&osgx::imgui::Widget::addStatsSection,
			"viewer"_a,
			"default_open"_a=false
		)
		.def(
			"addProfilerSection",
			&osgx::imgui::Widget::addProfilerSection,
			"view"_a,
			"sceneRoot"_a,
			"default_open"_a=false,
			"print_every"_a=0
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, osg::Node*, bool>(
				&osgx::imgui::Widget::addTextureSection
			),
			"view"_a,
			"root"_a,
			"default_open"_a=false
		)
		.def(
			"addTextureSection",
			py::overload_cast<osgViewer::View&, bool>(&osgx::imgui::Widget::addTextureSection),
			"view"_a,
			"default_open"_a=false
		)
	;

	m_imgui
		.def(
			"slider_float",
			&osgx::imgui::sliderFloat,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"format"_a="%.3f"
		)
		.def(
			"slider_float_nudge",
			&osgx::imgui::sliderFloatNudge,
			"label"_a,
			"value"_a,
			"min"_a,
			"max"_a,
			"step_pct"_a=0.01f,
			"format"_a="%.3f"
		)
		.def("text", &osgx::imgui::text, "text"_a)
		.def("separator", &osgx::imgui::separator)
		.def(
			"checkbox",
			&osgx::imgui::checkbox,
			"label"_a,
			"value"_a
		)
		.def(
			"input_text",
			&osgx::imgui::inputText,
			"label"_a,
			"value"_a,
			"max_length"_a=256,
			"enter_returns_true"_a=false
		)
		.def(
			"radio_group",
			&osgx::imgui::radioGroup,
			"value"_a,
			"labels"_a,
			"same_line"_a=true
		)
	;
#endif

	// osgx::platform -- X11/XRandr window helpers (alwaysOnTop, listMonitors, moveWindow) plus,
	// when built (see OSGX_WITH_EGL/OSGX_WITH_GBM in CMakeLists.txt), the EGL- and GBM/DRM-backed
	// GraphicsWindow factories. Moved here from OSG.py's pyosg/linux -- was never actually
	// OSG.py-specific, so osgx is the right home; see osgx/Linux.hpp.
	auto m_platform = m.def_submodule("platform", "osgx::platform - X11/EGL/GBM window helpers");

	m_platform.def(
		"alwaysOnTop",
		&osgx::platform::alwaysOnTop,
		"viewer"_a,
		"enabled"_a=true,
		"Pin the viewer's native X11 window above other windows (EWMH _NET_WM_STATE_ABOVE)."
	);

	py::class_<osgx::platform::Monitor>(m_platform, "Monitor")
		.def_readonly("name", &osgx::platform::Monitor::name)
		.def_readonly("x", &osgx::platform::Monitor::x)
		.def_readonly("y", &osgx::platform::Monitor::y)
		.def_readonly("width", &osgx::platform::Monitor::width)
		.def_readonly("height", &osgx::platform::Monitor::height)
		.def_readonly("primary", &osgx::platform::Monitor::primary)
		.def("__repr__", [](const osgx::platform::Monitor& self) {
			return
				"Monitor(name='"s + self.name + "', "
				"x="s + std::to_string(self.x) + ", "
				"y="s + std::to_string(self.y) + ", "
				"width="s + std::to_string(self.width) + ", "
				"height="s + std::to_string(self.height) + ", "
				"primary="s + (self.primary ? "True"s : "False"s) + ")"s
			;
		})
	;

	m_platform.def(
		"listMonitors",
		&osgx::platform::listMonitors,
		"Query the real XRandR monitor layout (position/size in root-window coordinates). Monitors "
		"are NOT assumed to be flush/adjacent -- use these rects directly for placement math."
	);

	m_platform.def(
		"moveWindow",
		&osgx::platform::moveWindow,
		"viewer"_a,
		"x"_a,
		"y"_a,
		"width"_a=-1,
		"height"_a=-1,
		"Reposition (and optionally resize) an already-realized X11 window, keeping OSG's own "
		"viewport bookkeeping in sync. Pass width/height <= 0 to keep the current size."
	);

#ifdef OSGX_EGL
	m_platform.def(
		"createEGLWindow",
		&osgx::platform::createEGLWindow,
		"traits"_a,
		"Create an X11 window driven by EGL (instead of GLX). Skeleton/proof-of-concept: assign "
		"the result to `camera.graphicsContext`."
	);
#endif

#ifdef OSGX_GBM
	m_platform.def(
		"createGBMWindow",
		&osgx::platform::createGBMWindow,
		"traits"_a,
		"Create a direct-scanout DRM/KMS+GBM window (no X11, no window manager). Skeleton/proof-"
		"of-concept: requires exclusive DRM master access, so it will fail under a running X "
		"server. Assign the result to `camera.graphicsContext`."
	);
#endif

	m_platform.def(
		"setCursorVisible",
		&osgx::platform::setCursorVisible,
		"view"_a,
		"visible"_a=true,
		"Show/hide the OS cursor for the view's current window."
	);

	m_platform.def(
		"warpPointer",
		&osgx::platform::warpPointer,
		"view"_a,
		"x"_a,
		"y"_a,
		"Warp the OS pointer to (x, y) in view/event coordinates (GUIEventAdapter.x/y space, not "
		"window-local pixels) without the jump itself registering as motion."
	);

	// Software hide+warp+accumulate mouse capture for turntable/FPS-style relative-motion look
	// controls. NOT true OS-level pointer confinement -- see osgx/Cursor.hpp.
	py::class_<
		osgx::platform::PointerCapture,
		osgGA::GUIEventHandler,
		osg::ref_ptr<osgx::platform::PointerCapture>
	>(m_platform, "PointerCapture")
		.def(py::init<osgViewer::View&>(), "view"_a)
		.def_property(
			"captured",
			&osgx::platform::PointerCapture::isCaptured,
			&osgx::platform::PointerCapture::setCaptured
		)
		.def("consume", &osgx::platform::PointerCapture::consume)
	;

	// osgx::gltf -- glTF 2.0 loader plus its optional osgx::gltf::pbribl PBR/IBL adapter, merged
	// from the formerly-separate osgGLTF repo/Python module 2026-07-30. Nested exactly like the
	// C++ namespace (osgx::gltf::shader, osgx::gltf::pbribl).
#ifdef OSGX_GLTF
	osgx::gltf::pbribl::registerShaderLibs();

	auto m_gltf = m.def_submodule("gltf", "osgx::gltf - glTF 2.0 loader + optional PBR/IBL adapter");

	auto m_gltf_shader = m_gltf.def_submodule(
		"shader",
		"Shader inputs and setup matching the scene state populated by the osgx::gltf loader"
	);

	m_gltf_shader.attr("TANGENT_ATTRIBUTE") = osgx::gltf::shader::TANGENT_ATTRIBUTE;
	m_gltf_shader.attr("JOINT_INDICES_ATTRIBUTE") = osgx::gltf::shader::JOINT_INDICES_ATTRIBUTE;
	m_gltf_shader.attr("JOINT_WEIGHTS_ATTRIBUTE") = osgx::gltf::shader::JOINT_WEIGHTS_ATTRIBUTE;
	m_gltf_shader.attr("TANGENT_ATTRIBUTE_NAME") = py::str(osgx::gltf::shader::TANGENT_ATTRIBUTE_NAME);
	m_gltf_shader.attr("JOINT_INDICES_ATTRIBUTE_NAME") =
		py::str(osgx::gltf::shader::JOINT_INDICES_ATTRIBUTE_NAME);
	m_gltf_shader.attr("JOINT_WEIGHTS_ATTRIBUTE_NAME") =
		py::str(osgx::gltf::shader::JOINT_WEIGHTS_ATTRIBUTE_NAME);
	m_gltf_shader.attr("MATERIAL_UBO_BINDING") = osgx::gltf::shader::MATERIAL_UBO_BINDING;
	m_gltf_shader.attr("JOINT_MATRICES_SSBO_BINDING") =
		osgx::gltf::shader::JOINT_MATRICES_SSBO_BINDING;
	m_gltf_shader.attr("BASE_COLOR_TEXTURE_UNIT") = osgx::gltf::shader::BASE_COLOR_TEXTURE_UNIT;
	m_gltf_shader.attr("NORMAL_TEXTURE_UNIT") = osgx::gltf::shader::NORMAL_TEXTURE_UNIT;
	m_gltf_shader.attr("ORM_TEXTURE_UNIT") = osgx::gltf::shader::ORM_TEXTURE_UNIT;
	m_gltf_shader.attr("EMISSIVE_TEXTURE_UNIT") = osgx::gltf::shader::EMISSIVE_TEXTURE_UNIT;
	m_gltf_shader.attr("BASE_COLOR_SAMPLER") = py::str(osgx::gltf::shader::BASE_COLOR_SAMPLER);
	m_gltf_shader.attr("NORMAL_SAMPLER") = py::str(osgx::gltf::shader::NORMAL_SAMPLER);
	m_gltf_shader.attr("ORM_SAMPLER") = py::str(osgx::gltf::shader::ORM_SAMPLER);
	m_gltf_shader.attr("EMISSIVE_SAMPLER") = py::str(osgx::gltf::shader::EMISSIVE_SAMPLER);
	m_gltf_shader.attr("ALPHA_MODE_UNIFORM") = py::str(osgx::gltf::shader::ALPHA_MODE_UNIFORM);
	m_gltf_shader.attr("ALPHA_CUTOFF_UNIFORM") = py::str(osgx::gltf::shader::ALPHA_CUTOFF_UNIFORM);
	m_gltf_shader.attr("ALPHA_MODE_OPAQUE") = osgx::gltf::shader::ALPHA_MODE_OPAQUE;
	m_gltf_shader.attr("ALPHA_MODE_MASK") = osgx::gltf::shader::ALPHA_MODE_MASK;
	m_gltf_shader.attr("ALPHA_MODE_BLEND") = osgx::gltf::shader::ALPHA_MODE_BLEND;
	m_gltf_shader.attr("MATERIAL_INPUTS") = py::str(osgx::gltf::shader::MATERIAL_INPUTS);

	m_gltf_shader
		.def("configureProgram", &osgx::gltf::shader::configureProgram, "program"_a)
		.def("configureStateSet", &osgx::gltf::shader::configureStateSet, "stateSet"_a)
	;

	auto m_gltf_pbribl = m_gltf.def_submodule(
		"pbribl",
		"Optional osgx::gltf rendering using the generic osgx PBR and IBL facilities"
	);

	m_gltf_pbribl.attr("GET_MATERIAL") = py::str(osgx::gltf::pbribl::GET_MATERIAL);
	m_gltf_pbribl.attr("SHADING_NORMAL") = py::str(osgx::gltf::pbribl::SHADING_NORMAL);
	m_gltf_pbribl.attr("EMISSIVE") = py::str(osgx::gltf::pbribl::EMISSIVE);
	m_gltf_pbribl.attr("ALPHA_COVERAGE") = py::str(osgx::gltf::pbribl::ALPHA_COVERAGE);
	m_gltf_pbribl.def("registerShaderLibs", &osgx::gltf::pbribl::registerShaderLibs);
	m_gltf_pbribl.def("resolveShaderLibs", [](const std::string& source) {
		return osgx::gltf::pbribl::resolveShaderLibs(source);
	}, "source"_a);

	py::class_<osgx::gltf::pbribl::PBRIBLEnvironment>(m_gltf_pbribl, "PBRIBLEnvironment")
		.def(py::init<>())
		.def_readwrite("root", &osgx::gltf::pbribl::PBRIBLEnvironment::root)
		.def_readwrite("envMap", &osgx::gltf::pbribl::PBRIBLEnvironment::envMap)
		.def_readwrite("brdfLUT", &osgx::gltf::pbribl::PBRIBLEnvironment::brdfLUT)
		.def_readwrite("diffuseEnv", &osgx::gltf::pbribl::PBRIBLEnvironment::diffuseEnv)
		.def("valid", &osgx::gltf::pbribl::PBRIBLEnvironment::valid)
	;

	py::class_<osgx::gltf::pbribl::PBRIBLScene>(m_gltf_pbribl, "PBRIBLScene")
		.def(py::init<>())
		.def_readwrite("node", &osgx::gltf::pbribl::PBRIBLScene::node)
		.def_readwrite("debugMode", &osgx::gltf::pbribl::PBRIBLScene::debugMode)
		.def_readwrite("disableNormalMap", &osgx::gltf::pbribl::PBRIBLScene::disableNormalMap)
		.def_readwrite(
			"disableRoughnessMap",
			&osgx::gltf::pbribl::PBRIBLScene::disableRoughnessMap
		)
		.def_readwrite(
			"disableSpecularAA",
			&osgx::gltf::pbribl::PBRIBLScene::disableSpecularAA
		)
		.def("valid", &osgx::gltf::pbribl::PBRIBLScene::valid)
	;

	m_gltf_pbribl.def(
		"preparePBRIBLEnvironment",
		py::overload_cast<const std::string&, const std::string&, int>(
			&osgx::gltf::pbribl::preparePBRIBLEnvironment
		),
		"ktx2Path"_a,
		"hdrPath"_a,
		"lutSize"_a=1024,
		"Load a pre-baked specular cubemap from ktx2Path; bake diffuse irradiance and the BRDF LUT "
		"live from hdrPath. Add root to the rendered scene graph so its PRE_RENDER passes can "
		"populate the generated textures."
	);
	m_gltf_pbribl.def(
		"preparePBRIBLEnvironment",
		py::overload_cast<const std::string&, int>(
			&osgx::gltf::pbribl::preparePBRIBLEnvironment
		),
		"hdrPath"_a,
		"lutSize"_a=1024,
		"Bake diffuse irradiance, the BRDF LUT, and GGX-prefiltered specular all live from hdrPath "
		"alone -- no pre-baked KTX2 required. Add root to the rendered scene graph so its "
		"PRE_RENDER passes can populate the generated textures."
	);
	m_gltf_pbribl.def(
		"createPBRIBLScene",
		&osgx::gltf::pbribl::createPBRIBLScene,
		"node"_a,
		"environment"_a,
		"iblIntensity"_a=1.0f,
		"diagnostics"_a=false,
		"Apply osgx::gltf's optional osgx-powered PBR/IBL renderer using prepared resources."
	);

	py::class_<osgx::gltf::SimplePlayer>(m_gltf, "SimplePlayer")
		.def(py::init<osg::Node*>(), "model"_a)
		.def("__bool__", [](const osgx::gltf::SimplePlayer& player) {
			return static_cast<bool>(player);
		})
		.def_property_readonly(
			"numAnimations",
			&osgx::gltf::SimplePlayer::getNumAnimations
		)
		.def("getAnimationName", &osgx::gltf::SimplePlayer::getAnimationName, "index"_a)
		.def(
			"playAnimation",
			py::overload_cast<std::size_t>(&osgx::gltf::SimplePlayer::playAnimation),
			"index"_a
		)
		.def(
			"playAnimation",
			py::overload_cast<const std::string&>(&osgx::gltf::SimplePlayer::playAnimation),
			"name"_a
		)
		.def_property_readonly(
			"currentAnimationIndex",
			[](const osgx::gltf::SimplePlayer& player) -> py::object {
				const std::size_t index = player.getCurrentAnimationIndex();
				return index == osgx::gltf::SimplePlayer::NoAnimation
					? py::none()
					: py::cast(index);
			}
		)
		.def_property_readonly(
			"currentAnimationName",
			&osgx::gltf::SimplePlayer::getCurrentAnimationName
		)
		.def_property(
			"playing",
			&osgx::gltf::SimplePlayer::getPlaying,
			&osgx::gltf::SimplePlayer::setPlaying
		)
		.def("togglePlaying", &osgx::gltf::SimplePlayer::togglePlaying)
		.def("restart", &osgx::gltf::SimplePlayer::restart)
	;

	m_gltf
		.def(
			"readNodeFileAsync",
			&readNodeFileAsync,
			"location"_a,
			"stop_event"_a,
			"loop"_a,
			"queue"_a,
			"job_id"_a,
			"Load a glTF/GLB file off the GIL, reporting (stage, current, total) progress and "
			"the final node through loop/queue via call_soon_threadsafe. Call via "
			"asyncio.to_thread(...); see examples/pyosg-async.py for the queue-draining pattern."
		)
	;

	m_gltf
		.def(
			"inspect",
			&inspectGLTF,
			"path"_a,
			"load_images"_a=false,
			"Parse a glTF/GLB file and return a structured Python summary."
		)
		.def(
			"inspect_json",
			&inspectGLTFJson,
			"path"_a,
			"load_images"_a=false,
			"indent"_a=2,
			"Parse a glTF/GLB file and return a JSON summary string."
		)
	;
#endif

	py::dict info;

	info["version"] = py::make_tuple(
		OSGX_VERSION_MAJOR,
		OSGX_VERSION_MINOR,
		OSGX_VERSION_PATCH
	);

	pyx::build_info(m, info);
}
