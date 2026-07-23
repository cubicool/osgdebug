#pragma once

#include "PBR.hpp"
#include "IBL.hpp"

namespace osgx {

// ================================================================================================
// osgx::gltf - the glue between osgGLTF's ReaderWriterGLTF (applyMaterial() in GLTFReader.hpp)
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
// match GLTFReader.hpp's std140 layout exactly) - everything else here is just GLSL glue on top.
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

// Reads MATERIAL_INPUTS into an osgx_Material (osgx::pbr::MATERIAL_STRUCT). Requires
// MATERIAL_INPUTS and MATERIAL_STRUCT already in scope.
//
// Every texture read here is conditioned on the matching `has*Map` flag rather than sampled
// unconditionally: a factor-only material (no baseColorTexture/metallicRoughnessTexture/
// occlusionTexture - common in the glTF-Sample-Models conformance set, e.g. Fox ships
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

	mat.F0 = mix(vec3(0.04), mat.albedo, mat.metallic);

	return mat;
}
)GLSL";

// TBN reconstructed per-pixel from screen-space derivatives of position/UV (Christian Schuler's
// "normal mapping without precomputed tangents" - http://www.thetenthplanet.de/archives/1180)
// when the mesh's own TANGENT is degenerate/absent, falling back to the vertex TANGENT otherwise.
// glTF's TANGENT accessor is optional and frequently absent (DamagedHelmet, MetalRoughSpheres,
// Fox in glTF-Sample-Models all ship without one); an unbound osg_Tangent attribute reads OpenGL's
// default (0,0,0,1), and normalizing that zero vector produces NaN, so the degenerate check here
// isn't optional. Requires MATERIAL_INPUTS already in scope.
inline constexpr const char* SHADING_NORMAL = R"GLSL(
vec3 osgx_GLTFShadingNormal(vec3 Ngeom, vec4 tangent, vec3 position, vec2 uv) {
	vec3 Nb = normalize(Ngeom);

	if(!bool(osgGLTF_material.hasNormalMap)) return Nb;

	// The Khronos reference normalizes in tangent space before applying TBN. Keeping that
	// normalization here matters when interpolation leaves TBN slightly non-orthonormal.
	vec3 tangentNormal = normalize(texture(osgGLTF_textures.normal, uv).rgb * 2.0 - 1.0);
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
// osgGLTF_alphaCutoff) discard;` itself - kept as a plain value here, not baked into a
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
// createPBRIBLScene - the one-call convenience helper requested after proving out, live, that
// osgx::gltf + osgx::pbr's full IBL_SPECULAR/F_MULTISCATTER + osgx::ibl's LAMBERTIAN_IRRADIANCE
// compose correctly against a real osgDB::readNodeFile()'d glTF model (confirmed against Batman +
// papermill.ktx2/papermill.hdr from OpenSceneGraph.py's pyosg-lighting/data, 2026-07-22). That
// prototype was ~90 lines of Python + GLSL wiring; this collapses it to one C++ (and, via bindings,
// one Python) call. A genuine C++ API first, same as everything else in this header - no
// pybind11 involved here at all; see ext/osgx-python.cpp for the separate binding.
//
// Defaults to osgx::ibl's baked Lambertian cubemap (computeLambertianCubeMap()) for diffuse IBL,
// not SH9 - pixel-parity with the Khronos glTF-Sample-Viewer reference was the explicit goal here
// (see TODO.md), and SH9's low-frequency basis measurably diverges from it in this environment.
// Both are baked and uploaded regardless (see `diffuseIBLMode` below) so a caller can A/B the two
// at runtime without reloading anything - useful for comparing bake cost and visual quality
// side-by-side. `PBRIBLScene::diffuseIBLMode` selects which one the shader actually samples:
// 0 = baked cubemap (default), 1 = SH9.
//
// IBL only, deliberately - no direct/punctual lights. A prior revision carried one ad hoc direct
// light (lightPos/lightColor/lightRadius uniforms); removed until osgx::pbr's *LightRig system
// (see OrbitLightRig in PBR.hpp) is finished and can plug in here as a proper, modular hook instead
// of a second hardcoded light source living next to it. Reach for the pattern this was built from
// (see OpenSceneGraph.py/examples/pyosg-voxelize.py's PBR_FALLBACK_FRAGMENT_SHADER_SRC, or the
// Python prototype this function replaces) if a scene needs direct lighting in the meantime.
// ================================================================================================

namespace detail {

// Full vertex/fragment shader pair, NOT registered as pragma-includable ShaderLib snippets (these
// are complete shaders with their own main(), not function-body fragments meant to be concatenated
// into a caller's shader) - resolved via resolveShaderLibs() at setup time inside createPBRIBLScene()
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

#pragma osgx::pbr MATERIAL_STRUCT, F_MULTISCATTER, TONEMAP_PBR_NEUTRAL
#pragma osgx::gltf MATERIAL_INPUTS, GET_MATERIAL, SHADING_NORMAL, EMISSIVE, ALPHA_COVERAGE
#pragma osgx::ibl LAMBERTIAN_IRRADIANCE, SH_IRRADIANCE

in vec3 vNGeom;
in vec3 vPosition;
in vec4 vTangent;
in vec2 vUV;

uniform vec3 emissiveFactor;
uniform mat4 osg_ViewMatrix;

uniform samplerCube envMap; // unit 5
uniform sampler2D brdfLUT; // unit 6
uniform samplerCube diffuseEnv; // unit 7
uniform float iblIntensity;
uniform vec3 iblSH[9];

// Runtime isolation, ported from OpenSceneGraph.py/pyosg-khronos-viewer.py's Diagnostics
// handler - lets a caller (see osgdebug-gltf.cpp) key-toggle which term is actually
// contributing to a surface, useful for isolating why a render looks wrong. debugMode:
// 0=combined, 1=diffuse only, 2=specular only. diffuseIBLMode picks the diffuse IBL source
// for A/B comparison: 0=baked Lambertian cubemap (diffuseEnv, default), 1=SH9 (iblSH).
uniform int debugMode;
uniform int disableNormalMap;
uniform int disableRoughnessMap;
uniform int diffuseIBLMode;

out vec4 fragColor;

struct Lighting {
	vec3 diffuse;
	vec3 specular;
};

vec3 osgx_ZUpToGltf(vec3 d) { return vec3(d.x, d.z, -d.y); }
vec3 osgx_LinearToSRGB(vec3 c) {
	return mix(12.92 * c, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
		step(vec3(0.0031308), c));
}

Lighting evaluateIBL(osgx_Material mat, vec3 N, vec3 V) {
	Lighting result;
	mat3 invView = transpose(mat3(osg_ViewMatrix));
	vec3 N_world = invView * N;
	vec3 V_world = invView * V;

	vec3 diffuseIrradiance = diffuseIBLMode == 0
		? osgx_LambertianIrradiance(N_world, diffuseEnv)
		: osgx_SHIrradiance(N_world, iblSH)
	;

	// The KTX2 prefilter has a terminal level that is not part of the Khronos GGX chain.
	// Match the reference viewer: roughness 1 selects the last filtered level, not that
	// terminal level.
	float maxMip = float(max(textureQueryLevels(envMap) - 2, 0));
	vec3 R = reflect(-V_world, N_world);
	vec3 R_gl = vec3(R.x, R.z, -R.y);
	vec3 prefiltered = textureLod(envMap, R_gl, mat.roughness * maxMip).rgb;

	// Matches pyosg-khronos-viewer.py's fresnel()/fd/fm/mix(...) exactly: two independent
	// multiscatter-Fresnel evaluations (dielectric F0=0.04, metal F0=albedo), mixed by metallic
	// AFTER Fresnel - NOT a single Fresnel evaluation of a pre-blended F0=mix(0.04,albedo,metallic).
	// The two are not equivalent: F_MultiScatter is nonlinear in F0 (see its Favg/(1-roughness)
	// clamp terms), so mixing F0 first and evaluating Fresnel once diverges from evaluating
	// Fresnel twice and mixing the result - most visible on partially-metallic materials at
	// grazing angles/higher roughness. This was the actual source of a real, camera-independent
	// specular mismatch found comparing BoomBox's handle (non-trivial metallic in its
	// metallicRoughnessTexture) against the Khronos reference.
	vec3 Fd = osgx_F_MultiScatter(N_world, V_world, mat.roughness, vec3(0.04), brdfLUT);
	vec3 Fm = osgx_F_MultiScatter(N_world, V_world, mat.roughness, mat.albedo, brdfLUT);
	vec3 kD_ibl = (1.0 - Fd) * (1.0 - mat.metallic);

	result.diffuse = diffuseIrradiance * mat.albedo * kD_ibl * mat.ao * iblIntensity;
	result.specular = prefiltered * mix(Fd, Fm, mat.metallic) * mat.ao * iblIntensity;

	return result;
}

void main() {
	float alpha = osgx_GLTFAlphaCoverage(vUV);

	if(osgGLTF_alphaMode == 1.0 && alpha < osgGLTF_alphaCutoff) discard;

	vec3 N = disableNormalMap != 0
		? normalize(vNGeom)
		: osgx_GLTFShadingNormal(vNGeom, vTangent, vPosition, vUV)
	;
	vec3 V = normalize(-vPosition);
	osgx_Material mat = osgx_GLTFGetMaterial(vUV, N);

	if(disableRoughnessMap != 0) mat.roughness = osgGLTF_material.roughnessFactor;

	// Match pyosg-khronos-viewer.py's material/coordinate diagnostics. These deliberately return
	// before lighting so a channel can be compared without IBL, Fresnel, or tonemapping involved.
	mat3 invView = transpose(mat3(osg_ViewMatrix));
	vec3 NgeomWorld = normalize(invView * vNGeom);
	vec3 Nworld = normalize(invView * N);

	if(debugMode == 3) { fragColor = vec4(osgx_LinearToSRGB(mat.albedo), alpha); return; }
	if(debugMode == 4) { fragColor = vec4(vec3(mat.roughness), alpha); return; }
	if(debugMode == 5) { fragColor = vec4(vec3(mat.metallic), alpha); return; }
	if(debugMode == 6) {
		vec3 raw = texture(osgGLTF_textures.normal, vUV).rgb;
		fragColor = vec4(bool(osgGLTF_material.hasNormalMap) ? normalize(raw * 2.0 - 1.0) * 0.5 + 0.5 : vec3(1.0), alpha);
		return;
	}
	if(debugMode == 7) {
		fragColor = vec4(bool(osgGLTF_material.hasNormalMap) ? texture(osgGLTF_textures.normal, vUV).rgb : vec3(1.0), alpha);
		return;
	}
	if(debugMode == 8) { fragColor = vec4(osgx_ZUpToGltf(NgeomWorld) * 0.5 + 0.5, alpha); return; }
	if(debugMode == 9) { fragColor = vec4(osgx_ZUpToGltf(Nworld) * 0.5 + 0.5, alpha); return; }
	vec3 tangentWorld = normalize(invView * vTangent.xyz);
	vec3 bitangentWorld = normalize(cross(NgeomWorld, tangentWorld)) * vTangent.w;
	if(debugMode == 10) { fragColor = vec4(osgx_ZUpToGltf(tangentWorld) * 0.5 + 0.5, alpha); return; }
	if(debugMode == 11) { fragColor = vec4(osgx_ZUpToGltf(bitangentWorld) * 0.5 + 0.5, alpha); return; }

	Lighting ambient = evaluateIBL(mat, N, V);
	vec3 emissive = debugMode == 0 ? osgx_GLTFEmissive(vUV, emissiveFactor) : vec3(0.0);

	vec3 surface = (debugMode == 1 || debugMode == 12)
		? ambient.diffuse
		: (debugMode == 2 || debugMode == 13) ? ambient.specular : ambient.diffuse + ambient.specular
	;

	vec3 color = surface + ((debugMode == 0 || debugMode == 14) ? osgx_GLTFEmissive(vUV, emissiveFactor) : vec3(0.0));

	// These three modes intentionally bypass PBR Neutral and gamma. They expose the linear
	// values immediately before output, so a comparison is not hidden by tone compression.
	if(debugMode >= 12) {
		fragColor = vec4(color, alpha);

		return;
	}

	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	fragColor = vec4(color, alpha);
}
)GLSL";

}

// Everything a caller gets back from createPBRIBLScene() - `lutCamera` MUST be added to the scene
// graph (anywhere, it's ABSOLUTE_RF - see makeBRDFLUTCamera()) or the BRDF LUT never actually
// bakes and every surface reads black split-sum values. `envMap`/`brdfLUT`/`diffuseEnv` are
// returned too in case the caller wants to reuse the same environment elsewhere (e.g. a skybox).
struct PBRIBLScene {
	osg::ref_ptr<osg::Camera> lutCamera;
	osg::ref_ptr<osg::TextureCubeMap> envMap;
	osg::ref_ptr<osg::Texture2D> brdfLUT;
	osg::ref_ptr<osg::TextureCubeMap> diffuseEnv;

	// Debug-mode uniforms (see FULL_PBR_FRAGMENT_SHADER_SRC) - returned so a caller can wire
	// up a diagnostic key handler (osgdebug-gltf.cpp does this) without doing a
	// getStateSet()->getUniform() lookup by name.
	osg::ref_ptr<osg::Uniform> debugMode;
	osg::ref_ptr<osg::Uniform> disableNormalMap;
	osg::ref_ptr<osg::Uniform> disableRoughnessMap;
	osg::ref_ptr<osg::Uniform> diffuseIBLMode; // 0 = baked Lambertian cubemap, 1 = SH9

	// False if either asset failed to load (see OSG_WARN for which) - createPBRIBLScene() still
	// returns normally rather than throwing (matches loadPrefilterCubemap()'s own
	// warn-and-return-null convention for asset loading, as opposed to registerShaderLibs()'s
	// throw-on-programmer-error convention, which this still goes through unchanged).
	bool valid() const {
		return lutCamera.valid() && envMap.valid() && brdfLUT.valid() && diffuseEnv.valid();
	}
};

// One-call "get PBR/IBL going from scratch" against an already-loaded glTF node: applies the full
// PBR/IBL shader above (material/lighting glue entirely from osgx::gltf/osgx::pbr/osgx::ibl, zero
// hand-copied GLSL), loads the prefiltered cubemap + bakes a Lambertian diffuse irradiance cubemap
// from the given paths, bakes the BRDF LUT, and wires every uniform/texture unit the shader
// needs. `node` is modified in place (StateSet gets the Program + uniforms, OVERRIDE'd same as
// apply_gltf_fallback_pbr() in pyosg-voxelize.py); the caller still owns adding `node` itself and
// the returned `lutCamera` to the scene graph.
//
inline PBRIBLScene createPBRIBLScene(
	osg::Node* node,
	const std::string& ktx2Path,
	const std::string& hdrPath,
	float iblIntensity = 1.0f, // matches pyosg-khronos-viewer.py's hardcoded 1.0 for parity
	int lutSize = 1024 // matches pyosg-khronos-viewer.py
) {
	// Idempotent (registerShaderLibs() no-ops on an identical re-registration - see
	// Shader.hpp) - called here so this really is a one-call helper. The Python
	// bindings register these three at module-import time (ext/osgx-python.cpp), which
	// is why a hand-assembled #pragma-based shader "just works" from Python without
	// this; a plain C++ caller has no equivalent implicit trigger, so resolveShaderLibs()
	// below would otherwise silently leave every #pragma osgx::* line unexpanded (no
	// error - see resolveShaderLibs()'s catalog-miss path in Shader.hpp) and hand raw,
	// uncompilable pragma text to the driver instead.
	pbr::registerShaderLibs();
	ibl::registerShaderLibs();
	gltf::registerShaderLibs();

	PBRIBLScene pis;

	pis.envMap = ibl::loadPrefilterCubemap(ktx2Path);

	if(!pis.envMap) return pis;

	pis.envMap->setUseHardwareMipMapGeneration(false);

	pis.brdfLUT = make_ref<osg::Texture2D>();
	pis.lutCamera = ibl::makeBRDFLUTCamera(lutSize, pis.brdfLUT);

	osg::ref_ptr<osg::Image> hdrImage = osgDB::readRefImageFile(hdrPath);

	if(!hdrImage) {
		OSG_WARN << "osgx::gltf::createPBRIBLScene: failed to load " << hdrPath << std::endl;

		pis.envMap = nullptr;
		pis.brdfLUT = nullptr;
		pis.lutCamera = nullptr;

		return pis;
	}

	// Poor man's profiling - both diffuse IBL techniques are baked from the same already-loaded
	// hdrImage (no extra I/O) so a caller can A/B them at runtime via `diffuseIBLMode` below
	// without reloading anything. computeSH() is a single O(W*H) pass over hdrImage directly;
	// computeLambertianCubeMap() is a real Monte Carlo convolution (O(6*size*size*samples)
	// bilinear samples) and is the slower of the two by design - this print is just to see how
	// much slower, in practice, for the environment actually being loaded.
	const osg::Timer_t shStart = tick();
	const ibl::SH9 sh = ibl::computeSH(hdrImage);
	const osg::Timer_t shEnd = tick();

	pis.diffuseEnv = ibl::computeLambertianCubeMap(hdrImage);

	const osg::Timer_t cubemapEnd = tick();

	OSG_NOTICE
		<< "osgx::gltf::createPBRIBLScene: computeSH() took "
		<< osg::Timer::instance()->delta_s(shStart, shEnd) << "s, computeLambertianCubeMap() took "
		<< osg::Timer::instance()->delta_s(shEnd, cubemapEnd) << "s"
		<< std::endl
	;

	auto* ss = node->getOrCreateStateSet();

	auto prog = make_ref<osg::Program>();

	prog->setName("osgx_gltf_PBRIBLScene");
	// osgGLTF stores glTF's optional TANGENT accessor in generic vertex attribute 7.
	// Bind it before linking, exactly as pyosg-khronos-viewer.py does; otherwise GLSL may
	// assign osg_Tangent to another generic attribute and normal mapping reads a default value.
	prog->addBindAttribLocation("osg_Tangent", 7);
	prog->addShader(new osg::Shader(osg::Shader::VERTEX, detail::FULL_PBR_VERTEX_SHADER));
	prog->addShader(new osg::Shader(
		osg::Shader::FRAGMENT,
		resolveShaderLibs(detail::FULL_PBR_FRAGMENT_SHADER_SRC)
	));

	ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setTextureAttributeAndModes(5, pis.envMap, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(6, pis.brdfLUT, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(7, pis.diffuseEnv, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("envMap", 5));
	ss->addUniform(new osg::Uniform("brdfLUT", 6));
	ss->addUniform(new osg::Uniform("diffuseEnv", 7));
	ss->addUniform(new osg::Uniform("iblIntensity", iblIntensity));
	ss->addUniform(new osg::Uniform("emissiveFactor", osg::Vec3(1.0f, 1.0f, 1.0f)));

	auto* shUniform = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "iblSH", 9);

	for(unsigned int i = 0; i < 9; i++) shUniform->setElement(i, sh.coeffs[i]);

	ss->addUniform(shUniform);

	// osgGLTF's GLTFReader binds the actual baseColor/normal/orm/emissive Texture2Ds to units
	// 0-3 per-geometry (GLTFReader.hpp's applyMaterial()), but deliberately stays shader-agnostic
	// and never sets the sampler *uniforms* that tell osgGLTF_textures which unit is which --
	// that's the shader glue's job (see MATERIAL_INPUTS's "unit N" comments above). Without this,
	// every sampler in the GLTFTextures struct silently defaults to unit 0 per the GLSL spec, so
	// normal/orm/emissive all end up reading the baseColor texture instead - corrupted shading
	// normals, scrambled roughness/metallic, and the whole baseColor image re-added as fake
	// "emissive" light. Same fix pyosg-khronos-viewer.py applies via its own uniforms.update(...).
	ss->addUniform(new osg::Uniform("osgGLTF_textures.baseColor", 0));
	ss->addUniform(new osg::Uniform("osgGLTF_textures.normal", 1));
	ss->addUniform(new osg::Uniform("osgGLTF_textures.orm", 2));
	ss->addUniform(new osg::Uniform("osgGLTF_textures.emissive", 3));

	pis.debugMode = new osg::Uniform("debugMode", 0);
	pis.disableNormalMap = new osg::Uniform("disableNormalMap", 0);
	pis.disableRoughnessMap = new osg::Uniform("disableRoughnessMap", 0);
	pis.diffuseIBLMode = new osg::Uniform("diffuseIBLMode", 0);

	ss->addUniform(pis.debugMode);
	ss->addUniform(pis.disableNormalMap);
	ss->addUniform(pis.disableRoughnessMap);
	ss->addUniform(pis.diffuseIBLMode);

	ss->setMode(GL_TEXTURE_CUBE_MAP_SEAMLESS, osg::StateAttribute::ON);

	return pis;
}

}

}
