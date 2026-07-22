#pragma once

#include "PBR.hpp"
#include "IBL.hpp"

namespace osgx {

// ================================================================================================
// osgx::gltf -- the glue between osgGLTF's ReaderWriterGLTF (applyMaterial() in GLTFReader.hpp)
// and osgx::pbr's material-agnostic BRDF math: reads the UBO/texture-unit contract the C++ loader
// populates per primitive into an osgx_Material (see osgx::pbr::MATERIAL_STRUCT), plus a couple
// of small glTF-specific fragment helpers (shading normal, emissive, alpha coverage) that need
// the same texture contract.
//
// Candidates identified by porting OpenSceneGraph.py/examples/pyosg-lighting/09-ibl.py's material-
// reading fragment code (getMaterial()/getShadingNormal()/getEmissive()/getAlphaCoverage()) down
// to OpenSceneGraph.py/examples/pyosg-voxelize.py's trimmed no-IBL-required PBR fallback shader --
// the same UBO-reading code was about to get copy-pasted a third time, which is exactly what
// osgx exists to avoid. MATERIAL_INPUTS is the fixed external contract (field order/types must
// match GLTFReader.hpp's std140 layout exactly) -- everything else here is just GLSL glue on top.
// ================================================================================================

namespace gltf {

// The osgGLTF loader's per-primitive contract: a std140 UBO of material scalars/flags (binding 0,
// populated by GLTFReader.hpp's applyMaterial()) plus four fixed texture units (baseColor=0,
// normal=1, orm=2, emissive=3) and two alpha-coverage uniforms. Include exactly once per shader --
// declaring `osgGLTF_Material`/`osgGLTF_textures` twice is a compile error, not a harmless
// redefinition. Every other snippet in this namespace assumes this one is already in scope.
inline constexpr const char* MATERIAL_INPUTS = R"GLSL(
layout(std140, binding = 0) uniform osgGLTF_Material {
	vec4 baseColorFactor;
	float roughnessFactor;
	float metallicFactor;
	float hasBaseColorMap;
	float hasMetallicRoughnessMap;
	float hasOcclusion;
	float hasNormalMap;
} osgGLTF_material;

struct GLTFTextures {
	sampler2D baseColor; // unit 0
	sampler2D normal; // unit 1
	sampler2D orm; // unit 2
	sampler2D emissive; // unit 3
};

uniform GLTFTextures osgGLTF_textures;

uniform float osgGLTF_alphaMode;
uniform float osgGLTF_alphaCutoff;
)GLSL";

// Reads MATERIAL_INPUTS (+ current shading normal N, for the specular-AA roughness clamp below)
// into an osgx_Material (osgx::pbr::MATERIAL_STRUCT). Requires MATERIAL_INPUTS and MATERIAL_STRUCT
// already in scope.
//
// Every texture read here is conditioned on the matching `has*Map` flag rather than sampled
// unconditionally: a factor-only material (no baseColorTexture/metallicRoughnessTexture/
// occlusionTexture -- common in the glTF-Sample-Models conformance set, e.g. Fox ships
// roughnessFactor=0.58 with no texture at all) would otherwise read an unbound texture unit as
// black/zero and silently discard the authored factor instead of falling back to it.
inline constexpr const char* GET_MATERIAL = R"GLSL(
osgx_Material osgx_GLTFGetMaterial(vec2 uv, vec3 N) {
	osgx_Material mat;

	mat.albedo = bool(osgGLTF_material.hasBaseColorMap)
		? texture(osgGLTF_textures.baseColor, uv).rgb
		: osgGLTF_material.baseColorFactor.rgb
	;
	mat.ao = bool(osgGLTF_material.hasOcclusion) ? texture(osgGLTF_textures.orm, uv).r : 1.0;
	mat.roughness = bool(osgGLTF_material.hasMetallicRoughnessMap)
		? texture(osgGLTF_textures.orm, uv).g * osgGLTF_material.roughnessFactor
		: osgGLTF_material.roughnessFactor
	;
	mat.metallic = bool(osgGLTF_material.hasMetallicRoughnessMap)
		? texture(osgGLTF_textures.orm, uv).b * osgGLTF_material.metallicFactor
		: osgGLTF_material.metallicFactor
	;

	// Specular AA: clamp roughness by how fast the shading normal rotates per pixel, so a bevel/
	// crease baked into a normal map doesn't render as an over-sharp aliased highlight.
	float normalDelta = max(
		max(abs(dFdx(N.x)), abs(dFdx(N.y))),
		max(abs(dFdy(N.x)), abs(dFdy(N.y)))
	);

	mat.roughness = max(mat.roughness, normalDelta);
	mat.F0 = mix(vec3(0.04), mat.albedo, mat.metallic);

	return mat;
}
)GLSL";

// TBN reconstructed per-pixel from screen-space derivatives of position/UV (Christian Schuler's
// "normal mapping without precomputed tangents" -- http://www.thetenthplanet.de/archives/1180)
// when the mesh's own TANGENT is degenerate/absent, falling back to the vertex TANGENT otherwise.
// glTF's TANGENT accessor is optional and frequently absent (DamagedHelmet, MetalRoughSpheres,
// Fox in glTF-Sample-Models all ship without one); an unbound osg_Tangent attribute reads OpenGL's
// default (0,0,0,1), and normalizing that zero vector produces NaN, so the degenerate check here
// isn't optional. Requires MATERIAL_INPUTS already in scope.
inline constexpr const char* SHADING_NORMAL = R"GLSL(
vec3 osgx_GLTFShadingNormal(vec3 Ngeom, vec4 tangent, vec3 position, vec2 uv) {
	vec3 Nb = normalize(Ngeom);

	if(!bool(osgGLTF_material.hasNormalMap)) return Nb;

	vec3 tangentNormal = texture(osgGLTF_textures.normal, uv).rgb * 2.0 - 1.0;
	vec3 T, B;

	// TODO: How much is this conditional hurting us? It might be worth looking into having two
	// separate functions instead...
	if(dot(tangent.xyz, tangent.xyz) > 1e-10) {
		T = normalize(tangent.xyz);
		B = normalize(cross(Nb, T)) * tangent.w;
	}

	else {
		vec3 q1 = dFdx(position);
		vec3 q2 = dFdy(position);
		vec2 st1 = dFdx(uv);
		vec2 st2 = dFdy(uv);

		T = normalize(q1 * st2.t - q2 * st1.t);
		B = -normalize(cross(Nb, T));
	}

	mat3 TBN = mat3(T, B, Nb);

	return normalize(TBN * tangentNormal);
}
)GLSL";

// Requires MATERIAL_INPUTS already in scope.
inline constexpr const char* EMISSIVE = R"GLSL(
vec3 osgx_GLTFEmissive(vec2 uv, vec3 emissiveFactor) {
	return texture(osgGLTF_textures.emissive, uv).rgb * emissiveFactor;
}
)GLSL";

// MASK-mode coverage test input (caller still does `if(osgGLTF_alphaMode == 1.0 && alpha <
// osgGLTF_alphaCutoff) discard;` itself -- kept as a plain value here, not baked into a
// discard, since some callers want the alpha for BLEND instead). Requires MATERIAL_INPUTS
// already in scope.
inline constexpr const char* ALPHA_COVERAGE = R"GLSL(
float osgx_GLTFAlphaCoverage(vec2 uv) {
	float alpha = bool(osgGLTF_material.hasBaseColorMap)
		? texture(osgGLTF_textures.baseColor, uv).a
		: 1.0
	;

	return alpha * osgGLTF_material.baseColorFactor.a;
}
)GLSL";

}

namespace gltf {

inline void registerShaderLibs() {
	static constexpr ShaderLib libs[] = {
		{"MATERIAL_INPUTS", "osgGLTF_Material", gltf::MATERIAL_INPUTS},
		{"GET_MATERIAL", "osgx_GLTFGetMaterial", gltf::GET_MATERIAL},
		{"SHADING_NORMAL", "osgx_GLTFShadingNormal", gltf::SHADING_NORMAL},
		{"EMISSIVE", "osgx_GLTFEmissive", gltf::EMISSIVE},
		{"ALPHA_COVERAGE", "osgx_GLTFAlphaCoverage", gltf::ALPHA_COVERAGE}
	};

	::osgx::registerShaderLibs("osgx::gltf", libs);
}

}

namespace gltf {

// ================================================================================================
// setupFullPBR -- the one-call convenience helper requested after proving out, live, that
// osgx::gltf + osgx::pbr's full IBL_SPECULAR/F_MULTISCATTER + osgx::ibl's SH_IRRADIANCE compose
// correctly against a real osgDB::readNodeFile()'d glTF model (confirmed against Batman +
// papermill.ktx2/papermill.hdr from OpenSceneGraph.py's pyosg-lighting/data, 2026-07-22). That
// prototype was ~90 lines of Python + GLSL wiring; this collapses it to one C++ (and, via bindings,
// one Python) call. A genuine C++ API first, same as everything else in this header -- no
// pybind11 involved here at all; see ext/osgx-python.cpp for the separate binding.
//
// Deliberately narrower than what a hand-assembled shader can do (ONE direct light, fixed uniform
// names/texture units) -- reach for the pattern this was built from (see
// OpenSceneGraph.py/examples/pyosg-voxelize.py's PBR_FALLBACK_FRAGMENT_SHADER_SRC, or the Python
// prototype this function replaces) if a scene needs more lights or different wiring.
// ================================================================================================

namespace detail {

// Full vertex/fragment shader pair, NOT registered as pragma-includable ShaderLib snippets (these
// are complete shaders with their own main(), not function-body fragments meant to be concatenated
// into a caller's shader) -- resolved via resolveShaderLibs() at setup time inside setupFullPBR()
// below, same mechanism a caller assembling their own shader by hand would use.
inline constexpr const char* FULL_PBR_VERTEX_SHADER = R"GLSL(
#version 460 core

in vec4 osg_Vertex;
in vec3 osg_Normal;
in vec4 osg_Tangent;
in vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;

out vec3 vNGeom;
out vec3 vPosition;
out vec4 vTangent;
out vec2 vUV;

void main() {
	vec4 eyePos = osg_ModelViewMatrix * osg_Vertex;
	vPosition = eyePos.xyz;
	vUV = osg_MultiTexCoord0;
	vNGeom = normalize(osg_NormalMatrix * osg_Normal);
	vTangent = vec4(osg_NormalMatrix * osg_Tangent.xyz, osg_Tangent.w);

	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
)GLSL";

inline constexpr const char* FULL_PBR_FRAGMENT_SHADER_SRC = R"GLSL(
#version 460 core

const float PI = 3.14159265359;

#pragma osgx::pbr MATERIAL_STRUCT, D_GGX, G_SCHLICK, G_SMITH, F_SCHLICK, F_SCHLICK_ROUGHNESS, DIRECT_SPECULAR, F_MULTISCATTER, IBL_SPECULAR, TONEMAP_PBR_NEUTRAL
#pragma osgx::gltf MATERIAL_INPUTS, GET_MATERIAL, SHADING_NORMAL, EMISSIVE, ALPHA_COVERAGE
#pragma osgx::ibl SH_IRRADIANCE

in vec3 vNGeom;
in vec3 vPosition;
in vec4 vTangent;
in vec2 vUV;

uniform vec3 emissiveFactor;
uniform mat4 osg_ViewMatrix;

uniform samplerCube envMap; // unit 5
uniform sampler2D brdfLUT; // unit 6
uniform vec3 iblSH[9];
uniform float iblIntensity;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform float lightRadius;

out vec4 fragColor;

vec3 evaluateDirectLighting(osgx_Material mat, vec3 N, vec3 V, float NdotV) {
	vec3 lEye = (osg_ViewMatrix * vec4(lightPos, 1.0)).xyz;
	vec3 lVec = lEye - vPosition;
	float dist = length(lVec);
	vec3 L = lVec / dist;
	float atten = 1.0 / (1.0 + (dist * dist) / (lightRadius * lightRadius));
	float NdotL = max(dot(N, L), 0.0);
	vec3 H = normalize(L + V);
	float HdotV = max(dot(H, V), 0.0);
	vec3 F = osgx_F_Schlick(HdotV, mat.F0);
	vec3 kD = (vec3(1.0) - F) * (1.0 - mat.metallic);
	vec3 diffuse = kD * mat.albedo / PI * NdotL;
	vec3 specular = osgx_DirectSpecular(N, V, L, NdotV, mat.roughness, mat.F0);

	return (diffuse + specular) * lightColor * atten;
}

vec3 evaluateIBL(osgx_Material mat, vec3 N, vec3 V, float NdotV) {
	mat3 invView = transpose(mat3(osg_ViewMatrix));
	vec3 N_world = invView * N;
	vec3 V_world = invView * V;

	vec3 F_ibl = osgx_F_Schlick_roughness(NdotV, mat.F0, mat.roughness);
	vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - mat.metallic);
	vec3 diffIBL = osgx_SHIrradiance(N_world, iblSH) * mat.albedo * kD_ibl;

	float maxMip = float(textureQueryLevels(envMap) - 1);
	vec3 specIBL = osgx_IBLSpecular(N_world, V_world, mat.F0, mat.roughness, envMap, brdfLUT, maxMip);

	return (diffIBL + specIBL) * mat.ao * iblIntensity;
}

void main() {
	float alpha = osgx_GLTFAlphaCoverage(vUV);
	if(osgGLTF_alphaMode == 1.0 && alpha < osgGLTF_alphaCutoff) discard;

	vec3 N = osgx_GLTFShadingNormal(vNGeom, vTangent, vPosition, vUV);
	vec3 V = normalize(-vPosition);
	osgx_Material mat = osgx_GLTFGetMaterial(vUV, N);
	float NdotV = max(dot(N, V), 0.0);

	vec3 Lo = evaluateDirectLighting(mat, N, V, NdotV);
	vec3 ambient = evaluateIBL(mat, N, V, NdotV);
	vec3 emissive = osgx_GLTFEmissive(vUV, emissiveFactor);

	vec3 color = ambient + Lo + emissive;
	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	fragColor = vec4(color, alpha);
}
)GLSL";

}

// Everything a caller gets back from setupFullPBR() -- `lutCamera` MUST be added to the scene
// graph (anywhere, it's ABSOLUTE_RF -- see makeBRDFLUTCamera()) or the BRDF LUT never actually
// bakes and every surface reads black split-sum values. `envMap`/`brdfLUT` are returned too in
// case the caller wants to reuse the same environment elsewhere (e.g. a skybox).
struct FullPBRSetup {
	osg::ref_ptr<osg::Camera> lutCamera;
	osg::ref_ptr<osg::TextureCubeMap> envMap;
	osg::ref_ptr<osg::Texture2D> brdfLUT;

	// False if either asset failed to load (see OSG_WARN for which) -- setupFullPBR() still
	// returns normally rather than throwing (matches loadPrefilterCubemap()'s own
	// warn-and-return-null convention for asset loading, as opposed to registerShaderLibs()'s
	// throw-on-programmer-error convention, which this still goes through unchanged).
	bool valid() const { return lutCamera.valid() && envMap.valid() && brdfLUT.valid(); }
};

// One-call "get PBR/IBL going from scratch" against an already-loaded glTF node: applies the full
// PBR/IBL shader above (material/lighting glue entirely from osgx::gltf/osgx::pbr/osgx::ibl, zero
// hand-copied GLSL), loads the prefiltered cubemap + computes SH9 diffuse irradiance from the given
// paths, bakes the BRDF LUT, and wires every uniform/texture unit the shader needs. `node` is
// modified in place (StateSet gets the Program + uniforms, OVERRIDE'd same as
// apply_gltf_fallback_pbr() in pyosg-voxelize.py); the caller still owns adding `node` itself and
// the returned `lutCamera` to the scene graph.
//
// `lightDir`/`lightDistance` position the one direct light relative to `node->getBound()` (not
// absolute world coordinates), so the same defaults land reasonably regardless of the model's own
// scale -- same reasoning as pyosg-voxelize.py's apply_gltf_fallback_pbr().
inline FullPBRSetup setupFullPBR(
	osg::Node* node,
	const std::string& ktx2Path,
	const std::string& hdrPath,
	float iblIntensity = 0.8f,
	const osg::Vec3& lightDir = osg::Vec3(0.45f, -0.75f, 0.9f),
	float lightDistance = 2.5f,
	const osg::Vec3& lightColor = osg::Vec3(4.0f, 4.0f, 3.8f),
	float lightRadiusScale = 4.0f,
	int lutSize = 512
) {
	// Idempotent (registerShaderLibs() no-ops on an identical re-registration -- see
	// Shader.hpp) -- called here so this really is a one-call helper. The Python
	// bindings register these three at module-import time (ext/osgx-python.cpp), which
	// is why a hand-assembled #pragma-based shader "just works" from Python without
	// this; a plain C++ caller has no equivalent implicit trigger, so resolveShaderLibs()
	// below would otherwise silently leave every #pragma osgx::* line unexpanded (no
	// error -- see resolveShaderLibs()'s catalog-miss path in Shader.hpp) and hand raw,
	// uncompilable pragma text to the driver instead.
	pbr::registerShaderLibs();
	ibl::registerShaderLibs();
	gltf::registerShaderLibs();

	FullPBRSetup setup;

	setup.envMap = ibl::loadPrefilterCubemap(ktx2Path);

	if(!setup.envMap) return setup; // already OSG_WARN'd by loadPrefilterCubemap()

	setup.envMap->setUseHardwareMipMapGeneration(false);

	setup.brdfLUT = make_ref<osg::Texture2D>();
	setup.lutCamera = ibl::makeBRDFLUTCamera(lutSize, setup.brdfLUT);

	osg::ref_ptr<osg::Image> hdrImage = osgDB::readRefImageFile(hdrPath);

	if(!hdrImage) {
		OSG_WARN << "osgx::gltf::setupFullPBR: failed to load " << hdrPath << std::endl;

		setup.envMap = nullptr;
		setup.brdfLUT = nullptr;
		setup.lutCamera = nullptr;

		return setup;
	}

	const ibl::SH9 sh = ibl::computeSH(hdrImage);
	auto* ss = node->getOrCreateStateSet();

	auto prog = make_ref<osg::Program>();

	prog->setName("osgx_gltf_fullPBR");
	prog->addShader(new osg::Shader(osg::Shader::VERTEX, detail::FULL_PBR_VERTEX_SHADER));
	prog->addShader(new osg::Shader(
		osg::Shader::FRAGMENT,
		resolveShaderLibs(detail::FULL_PBR_FRAGMENT_SHADER_SRC)
	));

	ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setTextureAttributeAndModes(5, setup.envMap, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(6, setup.brdfLUT, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("envMap", 5));
	ss->addUniform(new osg::Uniform("brdfLUT", 6));
	ss->addUniform(new osg::Uniform("iblIntensity", iblIntensity));
	ss->addUniform(new osg::Uniform("emissiveFactor", osg::Vec3(1.0f, 1.0f, 1.0f)));

	auto* shU = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "iblSH", 9);

	for(unsigned int i = 0; i < 9; i++) shU->setElement(i, sh.coeffs[i]);

	ss->addUniform(shU);

	const osg::BoundingSphere bound = node->getBound();
	const osg::Vec3 center = bound.valid() ? bound.center() : osg::Vec3(0, 0, 0);
	const float radius = bound.valid() ? std::max(bound.radius(), 1e-3f) : 1.0f;

	ss->addUniform(new osg::Uniform("lightPos", center + lightDir * (radius * lightDistance)));
	ss->addUniform(new osg::Uniform("lightColor", lightColor));
	ss->addUniform(new osg::Uniform("lightRadius", radius * lightRadiusScale));

	ss->setMode(GL_TEXTURE_CUBE_MAP_SEAMLESS, osg::StateAttribute::ON);

	return setup;
}

}

}
