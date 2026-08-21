#include "osgx-python.hpp"

// This whole file only compiles (and is only added to the source list, see CMakeLists.txt) when
// OSGX_BUILD_GLTF is on. It deliberately owns its tinygltf/osgx::gltf includes locally instead of
// pulling them into the shared osgx-python.hpp umbrella header -- tinygltf's headers are heavy
// and previously forced every single binding file in the old monolithic ext/osgx-python.cpp to
// pay their parse cost, even ones that never touch glTF at all.
#ifdef OSGX_GLTF

#include "osgx/gltf/PBRIBL.hpp"
#include "osgx/gltf/Reader.hpp"
#include "osgx/gltf/Shader.hpp"
#include "osgx/gltf/SimplePlayer.hpp"
#include "osgx/Shadow.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osgDB/FileNameUtils>

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS

#include <set>
#include <string>

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

namespace osgx_python {

// osgx::gltf -- glTF 2.0 loader plus its optional osgx::gltf::pbribl PBR/IBL adapter, merged from
// the formerly-separate osgGLTF repo/Python module 2026-07-30. Nested exactly like the C++
// namespace (osgx::gltf::shader, osgx::gltf::pbribl).
void bind_gltf(py::module_& m_gltf) {
	osgx::gltf::pbribl::registerShaderLibs();

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
	m_gltf_shader.attr("SKINNING_HOOK_IDENTITY") =
		py::str(osgx::gltf::shader::SKINNING_HOOK_IDENTITY);
	m_gltf_shader.attr("SKINNING_HOOK_LINEAR_BLEND") =
		py::str(osgx::gltf::shader::SKINNING_HOOK_LINEAR_BLEND);

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
		.def_readwrite("iblAxis", &osgx::gltf::pbribl::PBRIBLEnvironment::iblAxis)
		.def("valid", &osgx::gltf::pbribl::PBRIBLEnvironment::valid)
		.def_static(
			"prepare",
			py::overload_cast<const std::string&, int>(
				&osgx::gltf::pbribl::PBRIBLEnvironment::prepare
			),
			"hdrPath"_a,
			"lutSize"_a=1024,
			"Bake diffuse irradiance, the BRDF LUT, and GGX-prefiltered specular all live from hdrPath "
			"alone -- no pre-baked KTX2 required. Add root to the rendered scene graph so its "
			"PRE_RENDER passes can populate the generated textures."
		)
		.def_static(
			"load",
			py::overload_cast<const std::string&>(&osgx::gltf::pbribl::PBRIBLEnvironment::load),
			"manifestPath"_a,
			"Load a fully pre-baked osgx_pbribl environment manifest."
		)
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
		.def_readwrite(
			"iblDiffuseIntensity",
			&osgx::gltf::pbribl::PBRIBLScene::iblDiffuseIntensity
		)
		.def_readwrite(
			"iblSpecularIntensity",
			&osgx::gltf::pbribl::PBRIBLScene::iblSpecularIntensity
		)
		.def("valid", &osgx::gltf::pbribl::PBRIBLScene::valid)
		.def_static(
			"create",
			&osgx::gltf::pbribl::PBRIBLScene::create,
			"node"_a,
			"environment"_a,
			"iblDiffuseIntensity"_a=1.0f,
			"iblSpecularIntensity"_a=1.0f,
			"diagnostics"_a=false,
			"shadowMap"_a=nullptr,
			"tonemapHook"_a=nullptr,
			"skinningHook"_a=nullptr,
			"Apply osgx::gltf's optional osgx-powered PBR/IBL renderer using prepared resources. Pass "
			"an osgx.shadow.ShadowMap to shadow the key/directional light (osgx::LightSet index "
			"shadowMap.casterIndex); omit it for today's unshadowed behavior.\n\n"
			"skinningHook: a VERTEX osg.Shader defining osgx_gltf_ApplySkin(vec4, vec3, vec3), "
			"REPLACING the default identity passthrough -- pass "
			"osgx.gltf.shader.SKINNING_HOOK_LINEAR_BLEND (wrapped in "
			"osgx.gltf.pbribl.resolveShaderLibs()) to enable standard glTF joint-matrix skinning. "
			"Same substitutes-rather-than-adds contract as tonemapHook.\n\n"
			"tonemapHook: a FRAGMENT osg.Shader defining osgx_Tonemap(vec3), REPLACING the built-in "
			"PBR Neutral curve. It substitutes rather than adds -- GLSL permits one body per "
			"function, so attaching a second definition alongside the built-in is a link error, not "
			"an override."
		)
	;

	py::class_<osgx::gltf::pbribl::PBRIBLGBuffer>(m_gltf_pbribl, "PBRIBLGBuffer")
		.def(py::init<>())
		.def_readwrite("gbuffer", &osgx::gltf::pbribl::PBRIBLGBuffer::gbuffer)
		.def_readwrite("albedoTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::albedoTexture)
		.def_readwrite("normalTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::normalTexture)
		.def_readwrite("materialTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::materialTexture)
		.def_readwrite("emissiveTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::emissiveTexture)
		.def_readwrite("positionTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::positionTexture)
		.def_readwrite("depthTexture", &osgx::gltf::pbribl::PBRIBLGBuffer::depthTexture)
		.def("valid", &osgx::gltf::pbribl::PBRIBLGBuffer::valid)
		.def_static(
			"create",
			&osgx::gltf::pbribl::PBRIBLGBuffer::create,
			"node"_a,
			"width"_a,
			"height"_a,
			"Deferred-split geometry pass: writes material only (albedo/view-space normal/ORM/"
			"emissive + depth) to a PBRIBLGBuffer, no lighting. Feed the result to "
			"PBRIBLLightingScene.create()."
		)
	;

	py::class_<osgx::gltf::pbribl::PBRIBLLightingPassOptions>(m_gltf_pbribl, "PBRIBLLightingPassOptions")
		.def(py::init<>())
		.def_readwrite("tonemap", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::tonemap)
		.def_readwrite("tonemapHook", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::tonemapHook)
		.def_readwrite("shadowMap", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::shadowMap)
		.def_readwrite("aoTexture", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::aoTexture)
		.def_readwrite("diagnostics", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::diagnostics)
		.def_readwrite("colorTexture", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::colorTexture)
		.def_readwrite(
			"renderOrderNum", &osgx::gltf::pbribl::PBRIBLLightingPassOptions::renderOrderNum
		)
	;

	py::class_<osgx::gltf::pbribl::PBRIBLLightingScene>(m_gltf_pbribl, "PBRIBLLightingScene")
		.def(py::init<>())
		.def_readwrite("node", &osgx::gltf::pbribl::PBRIBLLightingScene::node)
		.def_readwrite(
			"iblDiffuseIntensity",
			&osgx::gltf::pbribl::PBRIBLLightingScene::iblDiffuseIntensity
		)
		.def_readwrite(
			"iblSpecularIntensity",
			&osgx::gltf::pbribl::PBRIBLLightingScene::iblSpecularIntensity
		)
		.def_readwrite(
			"mainViewMatrix",
			&osgx::gltf::pbribl::PBRIBLLightingScene::mainViewMatrix
		)
		.def_readwrite(
			"mainViewMatrixInverse",
			&osgx::gltf::pbribl::PBRIBLLightingScene::mainViewMatrixInverse
		)
		.def("valid", &osgx::gltf::pbribl::PBRIBLLightingScene::valid)
		.def_static(
			"create",
			&osgx::gltf::pbribl::PBRIBLLightingScene::create,
			"gbuffer"_a,
			"environment"_a,
			"mainCamera"_a,
			"iblDiffuseIntensity"_a=1.0f,
			"iblSpecularIntensity"_a=1.0f,
			"options"_a=osgx::gltf::pbribl::PBRIBLLightingPassOptions{},
			"Deferred-split lighting pass: a fullscreen quad reading `gbuffer` (position included, "
			"not reconstructed from depth), rotating it into world space via `mainCamera`'s real "
			"view matrix, running the same evaluateIBL()/osgx_DirectLighting() logic "
			"PBRIBLScene.create() does. Call .update() from a preDrawCallback on the "
			"FIRST PRE_RENDER camera in the scene graph every frame to keep it in sync as mainCamera "
			"moves -- NOT from mainCamera's own preDrawCallback or from application code after "
			"frame() returns, both of which hand this pass a stale matrix."
		)
		.def(
			"update",
			&osgx::gltf::pbribl::PBRIBLLightingScene::update,
			"mainCamera"_a,
			"Refreshes this scene's manually-maintained view-matrix uniforms from mainCamera's "
			"current matrices. Call from a preDrawCallback on the FIRST PRE_RENDER camera in the "
			"scene graph (by render order) every frame -- every PRE_RENDER camera finishes drawing "
			"before mainCamera's own preDrawCallback fires, so calling this from mainCamera's "
			"callback (or from application code after viewer.frame() returns) hands the lighting "
			"pass a one-frame-stale matrix relative to what the geometry pass just rendered with, "
			"which shows up as artifacts that worsen while the camera is moving."
		)
	;

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
}

}

#endif // OSGX_GLTF
