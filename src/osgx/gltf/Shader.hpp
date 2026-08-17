#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Program>
#include <osg/StateSet>
#include <osg/Uniform>

OSGX_ENABLE_WARNINGS

namespace osgx::gltf::shader {

// Position, normal, color, and texture coordinates use OSG's conventional arrays. These are the
// additional generic vertex attributes populated by the glTF loader.
inline constexpr unsigned int TANGENT_ATTRIBUTE = 7;
inline constexpr unsigned int JOINT_INDICES_ATTRIBUTE = 8;
inline constexpr unsigned int JOINT_WEIGHTS_ATTRIBUTE = 9;

inline constexpr char TANGENT_ATTRIBUTE_NAME[] = "osg_Tangent";
inline constexpr char JOINT_INDICES_ATTRIBUTE_NAME[] = "osgx_gltf_JointIndices";
inline constexpr char JOINT_WEIGHTS_ATTRIBUTE_NAME[] = "osgx_gltf_JointWeights";

inline constexpr unsigned int MATERIAL_BINDING = 0;
inline constexpr unsigned int JOINT_MATRICES_BINDING = 2;

// Texture units populated per primitive by the loader.
inline constexpr int BASE_COLOR_TEXTURE_UNIT = 0;
inline constexpr int NORMAL_TEXTURE_UNIT = 1;
inline constexpr int ORM_TEXTURE_UNIT = 2;
inline constexpr int EMISSIVE_TEXTURE_UNIT = 3;

inline constexpr char BASE_COLOR_SAMPLER[] = "osgx_gltf_textures.baseColor";
inline constexpr char NORMAL_SAMPLER[] = "osgx_gltf_textures.normal";
inline constexpr char ORM_SAMPLER[] = "osgx_gltf_textures.orm";
inline constexpr char EMISSIVE_SAMPLER[] = "osgx_gltf_textures.emissive";

inline constexpr char ALPHA_MODE_UNIFORM[] = "osgx_gltf_alphaMode";
inline constexpr char ALPHA_CUTOFF_UNIFORM[] = "osgx_gltf_alphaCutoff";

inline constexpr float ALPHA_MODE_OPAQUE = 0.0f;
inline constexpr float ALPHA_MODE_MASK = 1.0f;
inline constexpr float ALPHA_MODE_BLEND = 2.0f;

// Canonical GLSL declaration matching the data uploaded by the loader. osgx::gltf defines
// this interface but deliberately does not impose a particular material or lighting shader.
inline constexpr char MATERIAL_INPUTS[] = R"GLSL(
layout(std430, binding = 0) readonly buffer osgx_gltf_Material {
	vec4 baseColorFactor;
	float roughnessFactor;
	float metallicFactor;
	float hasBaseColorMap;
	float hasMetallicRoughnessMap;
	float hasOcclusion;
	float hasNormalMap;
} osgx_gltf_material;

struct GLTFTextures {
	sampler2D baseColor;
	sampler2D normal;
	sampler2D orm;
	sampler2D emissive;
};

uniform GLTFTextures osgx_gltf_textures;
uniform float osgx_gltf_alphaMode;
uniform float osgx_gltf_alphaCutoff;
)GLSL";

inline void configureProgram(osg::Program& program) {
	program.addBindAttribLocation(TANGENT_ATTRIBUTE_NAME, TANGENT_ATTRIBUTE);
	program.addBindAttribLocation(JOINT_INDICES_ATTRIBUTE_NAME, JOINT_INDICES_ATTRIBUTE);
	program.addBindAttribLocation(JOINT_WEIGHTS_ATTRIBUTE_NAME, JOINT_WEIGHTS_ATTRIBUTE);
}

inline void configureStateSet(osg::StateSet& stateSet) {
	stateSet.addUniform(new osg::Uniform(BASE_COLOR_SAMPLER, BASE_COLOR_TEXTURE_UNIT));
	stateSet.addUniform(new osg::Uniform(NORMAL_SAMPLER, NORMAL_TEXTURE_UNIT));
	stateSet.addUniform(new osg::Uniform(ORM_SAMPLER, ORM_TEXTURE_UNIT));
	stateSet.addUniform(new osg::Uniform(EMISSIVE_SAMPLER, EMISSIVE_TEXTURE_UNIT));
}

}
