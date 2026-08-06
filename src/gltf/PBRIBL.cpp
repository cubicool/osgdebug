#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS

#include "osgx/gltf/PBRIBL.hpp"
#include "osgx/gltf/Shader.hpp"

#include <osgx/GGXPrefilter.hpp>
#include <osgx/IBL.hpp>
#include <osgx/PBR.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osg/Notify>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>
#include <osgDB/ReadFile>

OSGX_ENABLE_WARNINGS

#include <filesystem>

// ================================================================================================
// osgx::gltf::pbribl - the glue between osgGLTF's loader material interface
// and osgx::pbr's material-agnostic BRDF math: reads the UBO/texture-unit interface the C++ loader
// populates per primitive into an osgx_Material (see osgx::pbr::MATERIAL_STRUCT), plus a couple
// of small glTF-specific fragment helpers (shading normal, emissive, alpha coverage) that need
// the same texture interface.
//
// Candidates identified by porting OpenSceneGraph.py/examples/pyosg-lighting/09-ibl.py's material-
// reading fragment code (getMaterial()/getShadingNormal()/getEmissive()/getAlphaCoverage()) down
// to OpenSceneGraph.py/examples/pyosg-voxelize.py's trimmed no-IBL-required PBR fallback shader --
// the same UBO-reading code was about to get copy-pasted a third time, which is exactly what
// osgGLTF exists to avoid. Shader.hpp's MATERIAL_INPUTS is the fixed external interface (field
// order/types must match Material.cpp's std140 layout exactly); everything else is GLSL glue.
// ================================================================================================

namespace osgx::gltf::pbribl {

// Reads MATERIAL_INPUTS into an osgx_Material (osgx::pbr::MATERIAL_STRUCT). Requires
// MATERIAL_INPUTS and MATERIAL_STRUCT already in scope.
//
// Every texture read here is conditioned on the matching `has*Map` flag rather than sampled
// unconditionally: a factor-only material (no baseColorTexture/metallicRoughnessTexture/
// occlusionTexture - common in the glTF-Sample-Models conformance set, e.g. Fox ships
// roughnessFactor=0.58 with no texture at all) would otherwise read an unbound texture unit as
// black/zero and silently discard the authored factor instead of falling back to it.
const char GET_MATERIAL[] = R"GLSL(
osgx_Material osgGLTF_GetMaterial(vec2 uv, vec3 N) {
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
const char SHADING_NORMAL[] = R"GLSL(
vec3 osgGLTF_ShadingNormal(vec3 Ngeom, vec4 tangent, vec3 position, vec2 uv) {
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

		// Derive both tangent axes from position and UV derivatives.  The
		// determinant carries the UV handedness: constructing B from a fixed
		// +/-cross(N, T) works on only one side of a mirrored UV layout.
		float determinant = st1.s * st2.t - st1.t * st2.s;

		if(abs(determinant) <= 1e-10) return Nb;

		float inverseDeterminant = 1.0 / determinant;
		T = (q1 * st2.t - q2 * st1.t) * inverseDeterminant;
		B = (q2 * st1.s - q1 * st2.s) * inverseDeterminant;

		// Projection keeps the reconstructed basis orthogonal to an interpolated
		// vertex normal, and the final Gram-Schmidt step preserves B's derived
		// handedness rather than imposing one.
		T -= Nb * dot(Nb, T);
		B -= Nb * dot(Nb, B);

		if(dot(T, T) <= 1e-10 || dot(B, B) <= 1e-10) return Nb;

		T = normalize(T);
		B -= T * dot(T, B);

		if(dot(B, B) <= 1e-10) return Nb;

		// NormalTangentTest verifies the glTF normal-map convention for this
		// runtime-derived basis. Its bitangent is opposite the derivative-space
		// orientation above; invert only this no-authored-tangent fallback.
		B = -normalize(B);
	}

	mat3 TBN = mat3(T, B, Nb);

	return normalize(TBN * tangentNormal);
}
)GLSL";

// Requires MATERIAL_INPUTS already in scope.
const char EMISSIVE[] = R"GLSL(
vec3 osgGLTF_Emissive(vec2 uv, vec3 emissiveFactor) {
	return texture(osgGLTF_textures.emissive, uv).rgb * emissiveFactor;
}
)GLSL";

// MASK-mode coverage test input (caller still does `if(osgGLTF_alphaMode == 1.0 && alpha <
// osgGLTF_alphaCutoff) discard;` itself - kept as a plain value here, not baked into a
// discard, since some callers want the alpha for BLEND instead). Requires MATERIAL_INPUTS
// already in scope.
const char ALPHA_COVERAGE[] = R"GLSL(
float osgGLTF_AlphaCoverage(vec2 uv) {
	float alpha = bool(osgGLTF_material.hasBaseColorMap)
		? texture(osgGLTF_textures.baseColor, uv).a
		: 1.0
	;

	return alpha * osgGLTF_material.baseColorFactor.a;
}
)GLSL";

}

namespace osgx::gltf::pbribl {

void registerShaderLibs() {
	static const osgx::ShaderLib libs[] = {
		{"MATERIAL_INPUTS", "osgGLTF_Material", shader::MATERIAL_INPUTS},
		{"GET_MATERIAL", "osgGLTF_GetMaterial", GET_MATERIAL},
		{"SHADING_NORMAL", "osgGLTF_ShadingNormal", SHADING_NORMAL},
		{"EMISSIVE", "osgGLTF_Emissive", EMISSIVE},
		{"ALPHA_COVERAGE", "osgGLTF_AlphaCoverage", ALPHA_COVERAGE}
	};

	osgx::registerShaderLibs("osgGLTF", libs);
}

std::string resolveShaderLibs(std::string_view source) {
	osgx::pbr::registerShaderLibs();
	osgx::ibl::registerShaderLibs();

	registerShaderLibs();

	return osgx::resolveShaderLibs(std::string(source));
}

}

namespace osgx::gltf::pbribl {

// ================================================================================================
// createPBRIBLScene - the one-call convenience helper requested after proving out, live, that
// osgGLTF material glue + osgx::pbr's F_MULTISCATTER + osgx::ibl's LAMBERTIAN_IRRADIANCE
// compose correctly against a real osgDB::readNodeFile()'d glTF model (confirmed against Batman +
// papermill.ktx2/papermill.hdr from OpenSceneGraph.py's pyosg-lighting/data, 2026-07-22). That
// prototype was ~90 lines of Python + GLSL wiring; this collapses it to one C++ (and, via bindings,
// one Python) call. A genuine C++ API first, with a separate binding in ext/osgGLTF-python.cpp.
//
// Uses osgx::ibl's frame-driven GPU-baked Lambertian cubemap for diffuse IBL.
// Pixel-parity with the Khronos glTF-Sample-Viewer reference is the explicit goal here, and SH9's
// low-frequency basis measurably washes out the high-contrast environments used by this helper.
// SH9 remains available as an independent osgx::ibl utility for callers that prefer its compact,
// low-cost representation; it is deliberately not part of this reference-quality convenience path.
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
constexpr const char FULL_PBR_VERTEX_SHADER[] = R"GLSL(
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

constexpr const char FULL_PBR_FRAGMENT_SHADER_SRC[] = R"GLSL(
#version 460 core
#pragma import_defines ( OSGX_PBRIBL_DIAGNOSTICS )

const float PI = 3.14159265359;

#pragma osgx::pbr MATERIAL_STRUCT, F_MULTISCATTER, SPECULAR_AA, TONEMAP_PBR_NEUTRAL
#pragma osgGLTF MATERIAL_INPUTS, GET_MATERIAL, SHADING_NORMAL, EMISSIVE, ALPHA_COVERAGE

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
// KTX/OpenGL cubemap lookup basis. A prepared environment may override this for a legacy or
// application-specific cube convention.
uniform vec3 iblAxisX;
uniform vec3 iblAxisY;
uniform vec3 iblAxisZ;
#ifdef OSGX_PBRIBL_DIAGNOSTICS
// Runtime isolation, ported from OpenSceneGraph.py/pyosg-khronos-viewer.py's Diagnostics
// handler - lets a caller (see osggltf-viewer.cpp) key-toggle which term is actually
// contributing to a surface, useful for isolating why a render looks wrong. debugMode:
// 0=combined, 1=diffuse only, 2=specular only.
uniform int debugMode;
uniform int disableNormalMap;
uniform int disableRoughnessMap;
uniform int disableSpecularAA;
#endif

out vec4 fragColor;

struct Lighting {
	vec3 diffuse;
	vec3 specular;
};

vec3 osgx_ZUpToGltf(vec3 d) { return vec3(d.x, d.z, -d.y); }
vec3 osgx_OrientIBL(vec3 d) {
	return vec3(dot(d, iblAxisX), dot(d, iblAxisY), dot(d, iblAxisZ));
}
vec3 osgx_LinearToSRGB(vec3 c) {
	return mix(12.92 * c, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
		step(vec3(0.0031308), c));
}

Lighting evaluateIBL(osgx_Material mat, vec3 N, vec3 V) {
	Lighting result;
	mat3 invView = transpose(mat3(osg_ViewMatrix));
	vec3 N_world = invView * N;
	vec3 V_world = invView * V;

	vec3 diffuseIrradiance = texture(diffuseEnv, osgx_OrientIBL(osgx_ZUpToGltf(N_world))).rgb;

	// The KTX2 prefilter has a terminal level that is not part of the Khronos GGX chain.
	// Match the reference viewer: roughness 1 selects the last filtered level, not that
	// terminal level.
	float maxMip = float(max(textureQueryLevels(envMap) - 2, 0));
	vec3 R = reflect(-V_world, N_world);
	vec3 R_gl = osgx_OrientIBL(vec3(R.x, R.z, -R.y));
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
	float alpha = osgGLTF_AlphaCoverage(vUV);

	if(osgGLTF_alphaMode == 1.0 && alpha < osgGLTF_alphaCutoff) discard;

	vec3 N = osgGLTF_ShadingNormal(vNGeom, vTangent, vPosition, vUV);

#ifdef OSGX_PBRIBL_DIAGNOSTICS
	if(disableNormalMap != 0) N = normalize(vNGeom);
#endif
	vec3 V = normalize(-vPosition);
	osgx_Material mat = osgGLTF_GetMaterial(vUV, N);


#ifdef OSGX_PBRIBL_DIAGNOSTICS
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
#endif

	// Widens roughness under high-curvature/low-roughness shading normals so the mirror-like
	// reflection vector doesn't alias between neighboring fragments at low MSAA sample counts
	// (a beveled metal trim is the motivating case - see osgx_SpecularAA). Debug channels above
	// intentionally read mat.roughness before this so they show the authored/textured value, not
	// the screen-space-widened one used for actual shading.
	float aaRoughness = osgx_SpecularAA(N, mat.roughness);

#ifdef OSGX_PBRIBL_DIAGNOSTICS
	if(disableSpecularAA == 0) mat.roughness = aaRoughness;
#else
	mat.roughness = aaRoughness;
#endif

	Lighting ambient = evaluateIBL(mat, N, V);
	vec3 surface = ambient.diffuse + ambient.specular;
	vec3 emissive = osgGLTF_Emissive(vUV, emissiveFactor);

#ifdef OSGX_PBRIBL_DIAGNOSTICS
	surface = (debugMode == 1 || debugMode == 12)
		? ambient.diffuse
		: (debugMode == 2 || debugMode == 13) ? ambient.specular : ambient.diffuse + ambient.specular
	;
	emissive = (debugMode == 0 || debugMode == 14) ? emissive : vec3(0.0);
#endif

	vec3 color = surface + emissive;

#ifdef OSGX_PBRIBL_DIAGNOSTICS
	// These three modes intentionally bypass PBR Neutral and gamma. They expose the linear
	// values immediately before output, so a comparison is not hidden by tone compression.
	if(debugMode >= 12) {
		fragColor = vec4(color, alpha);

		return;
	}
#endif

	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	fragColor = vec4(color, alpha);
}
)GLSL";

}

bool PBRIBLEnvironment::valid() const {
	return envMap.valid() && brdfLUT.valid() && diffuseEnv.valid();
}

bool PBRIBLScene::valid() const { return node.valid(); }

// One-call "get PBR/IBL going from scratch" against an already-loaded glTF node: applies the full
// PBR/IBL shader above (osgGLTF material glue plus generic osgx::pbr/osgx::ibl, zero
// hand-copied GLSL), loads the prefiltered cubemap + bakes a Lambertian diffuse irradiance cubemap
// from the given paths, builds frame-driven GPU bakes for the Lambertian diffuse cubemap and BRDF
// LUT, and wires every uniform/texture unit the shader needs. `node` is modified in place
// (StateSet gets the Program + uniforms, OVERRIDE'd same as apply_gltf_fallback_pbr() in
// pyosg-voxelize.py); the caller owns adding the prepared environment root and returned scene
// node to the graph. `diagnostics=false` is the production path;
// setting it true compiles the debug channels and creates their uniforms. The three diagnostic
// uniform pointers are null in production.
//
// Fully dynamic overload: bakes the GGX-prefiltered specular cubemap live instead of loading one
// from a KTX2 file, calling the exact same osgx::ibl::createGGXPrefilterScene() workflow
// osggltf-iblbake-gpu already wraps to write that KTX2 to disk in the first place -- no readback,
// no CPU round-trip, no temporary file. GGXPrefilterScene::prefilterTexture is the live render-
// target cubemap the bake's PRE_RENDER passes write into; it is immediately valid to bind (same
// contract as the existing Lambertian diffuseEnv below), it just isn't correct until
// specularBakeRoot's passes have actually run a few frames of whatever viewer the caller adds
// environment.root to. GGXPrefilterReadback (glFinish-gated CPU readback) is deliberately unused
// here -- that machinery exists only for osggltf-iblbake-gpu's serialize-to-KTX2 use case.
//
// prefilterSize=256 matches what this session's Khronos-parity comparisons actually validated
// (GGXPrefilterOptions's own default of 128 has not been checked against the reference); revisit
// once the mip-count/roughness-convention options from TODO.md's "Generic osgx and IBL later work"
// land; and see that same TODO entry for what is deliberately NOT yet handled here (a not-ready-
// yet texture read is undefined driver contents, not a defined placeholder value).
PBRIBLEnvironment preparePBRIBLEnvironment(const std::string& hdrPath, int lutSize) {
	PBRIBLEnvironment environment;

	auto hdrImage = osgDB::readRefImageFile(hdrPath);

	if(!hdrImage) return environment;

	auto lut = osgx::ibl::sharedBRDFLUT(lutSize);

	environment.brdfLUT = lut.texture;
	environment.lutCamera = lut.camera; // null once another environment already baked this size

	auto diffuseBake = osgx::ibl::createLambertianBakeScene(hdrImage);

	environment.diffuseBakeRoot = diffuseBake.root;
	environment.diffuseEnv = diffuseBake.diffuseTexture;

	osgx::ibl::GGXPrefilterOptions specularOptions;

	specularOptions.prefilterSize = 256;

	auto specularBake = osgx::ibl::createGGXPrefilterScene(hdrImage, specularOptions);

	environment.specularBakeRoot = specularBake.root;
	environment.envMap = specularBake.prefilterTexture;

	environment.root = osgx::make_ref<osg::Group>();

	if(environment.lutCamera) environment.root->addChild(environment.lutCamera);

	environment.root->addChild(environment.diffuseBakeRoot);
	environment.root->addChild(environment.specularBakeRoot);

	return environment;
}

namespace {

// `value.Get(key)` asserts IsObject() on the receiver, so every lookup below guards that first --
// a missing/malformed role (e.g. no "diffuse" object at all) must decode to defaults, not assert.
std::string decodeString(const tinygltf::Value& value, const char* key) {
	if(!value.IsObject()) return {};

	const auto& found = value.Get(key);

	return found.IsString() ? found.Get<std::string>() : std::string();
}

int decodeInt(const tinygltf::Value& value, const char* key, int fallback) {
	if(!value.IsObject()) return fallback;

	const auto& found = value.Get(key);

	return found.IsInt() ? found.GetNumberAsInt() : fallback;
}

IBLEnvironmentManifest decodeIBLEnvironment(const tinygltf::Value& entry) {
	IBLEnvironmentManifest manifest;

	if(!entry.IsObject()) return manifest;

	const auto& specular = entry.Get("specular");

	manifest.specular.uri = decodeString(specular, "uri");
	manifest.specular.prefilterSize = decodeInt(specular, "prefilterSize", 0);
	manifest.specular.lowestMipLevel = decodeInt(specular, "lowestMipLevel", 0);
	manifest.diffuse.uri = decodeString(entry.Get("diffuse"), "uri");

	const auto& brdfLUT = entry.Get("brdfLUT");

	manifest.brdfLUT.uri = decodeString(brdfLUT, "uri");
	manifest.brdfLUT.builtin = decodeString(brdfLUT, "builtin");
	manifest.brdfLUT.size = decodeInt(brdfLUT, "size", 1024);

	return manifest;
}

}

std::vector<IBLEnvironmentManifest> decodeIBLEnvironments(const tinygltf::Value& extensionValue) {
	std::vector<IBLEnvironmentManifest> result;

	if(!extensionValue.IsObject()) return result;

	const auto& environments = extensionValue.Get("environments");

	if(!environments.IsArray()) return result;

	for(std::size_t i = 0; i < environments.ArrayLen(); i++) result.push_back(
		decodeIBLEnvironment(environments.Get(static_cast<int>(i)))
	);

	return result;
}

PBRIBLEnvironment loadPBRIBLEnvironment(const IBLEnvironmentManifest& manifest, const std::string& baseDir) {
	PBRIBLEnvironment environment;

	if(!manifest.specular.valid() || !manifest.diffuse.valid() || !manifest.brdfLUT.valid()) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: manifest is missing a required resource" << std::endl;

		return environment;
	}

	const std::filesystem::path base(baseDir);

	// loadPrefilterCubemap() is content-agnostic despite its name -- it just loads a KTX2 as a
	// TextureCubeMap, which is equally correct for the Lambertian diffuse cube as for GGX specular.
	environment.envMap = osgx::ibl::loadPrefilterCubemap((base / manifest.specular.uri).string());

	if(!environment.envMap) return environment;

	environment.diffuseEnv = osgx::ibl::loadPrefilterCubemap((base / manifest.diffuse.uri).string());

	if(!environment.diffuseEnv) return environment;

	if(!manifest.brdfLUT.builtin.empty()) {
		if(manifest.brdfLUT.builtin != "osgx:split-sum-ggx-v1" || manifest.brdfLUT.size < 1) {
			OSG_WARN
				<< "osgx::gltf::pbribl::loadPBRIBLEnvironment: unsupported built-in BRDF LUT "
				<< manifest.brdfLUT.builtin << std::endl
			;

			return environment;
		}

		auto lut = osgx::ibl::sharedBRDFLUT(manifest.brdfLUT.size);

		environment.brdfLUT = lut.texture;
		environment.lutCamera = lut.camera;

		if(environment.lutCamera) {
			environment.root = osgx::make_ref<osg::Group>();
			environment.root->addChild(environment.lutCamera);
		}

		return environment;
	}

	auto lutImage = osgDB::readRefImageFile((base / manifest.brdfLUT.uri).string());

	if(!lutImage) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: failed to load " << manifest.brdfLUT.uri << std::endl;

		return environment;
	}

	environment.brdfLUT = osgx::make_ref<osg::Texture2D>();
	environment.brdfLUT->setImage(lutImage);
	environment.brdfLUT->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	environment.brdfLUT->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	environment.brdfLUT->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	environment.brdfLUT->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	return environment;
}

PBRIBLEnvironment loadPBRIBLEnvironment(const std::string& manifestPath) {
	tinygltf::TinyGLTF loader;
	tinygltf::Model document;
	std::string error, warning;

	if(!loader.LoadASCIIFromFile(&document, &error, &warning, manifestPath)) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: failed to load " << manifestPath << ": " << error << std::endl;

		return {};
	}

	if(!warning.empty()) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: " << manifestPath << ": " << warning << std::endl;
	}

	const auto it = document.extensions.find("osgx_pbribl");

	if(it == document.extensions.end()) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: " << manifestPath << " has no osgx_pbribl extension" << std::endl;

		return {};
	}

	auto environments = decodeIBLEnvironments(it->second);

	if(environments.empty()) {
		OSG_WARN << "osgx::gltf::pbribl::loadPBRIBLEnvironment: " << manifestPath << " declares no environments" << std::endl;

		return {};
	}

	const std::string baseDir = std::filesystem::path(manifestPath).parent_path().string();
	const auto& manifest = environments.front();
	auto environment = loadPBRIBLEnvironment(manifest, baseDir);
	const std::string brdfLUT = !manifest.brdfLUT.uri.empty() ? manifest.brdfLUT.uri : manifest.brdfLUT.builtin;

	if(environment.valid()) {
		OSG_NOTICE
			<< "osgx::gltf::pbribl: loaded pre-baked environment manifest \"" << manifestPath
			<< "\" (specular=\"" << manifest.specular.uri
			<< "\", diffuse=\"" << manifest.diffuse.uri
			<< "\", brdfLUT=\"" << brdfLUT << "\")"
			<< std::endl
		;
	}

	return environment;
}

PBRIBLScene createPBRIBLScene(
	osg::Node* node,
	const PBRIBLEnvironment& environment,
	float iblIntensity,
	bool diagnostics
) {
	// osgx::gltf::pbribl::resolveShaderLibs() below idempotently registers the generic osgx catalogs and
	// this component's glTF catalog before expansion. This keeps the one-call helper independent of
	// module-import order and avoids handing raw, uncompilable pragma text to the driver.
	PBRIBLScene pis;

	if(!node || !environment.valid()) return pis;

	pis.node = node;

	auto* ss = node->getOrCreateStateSet();
	auto prog = osgx::make_ref<osg::Program>();

	// osgGLTF stores glTF's optional TANGENT accessor in generic vertex attribute 7.
	// Bind it before linking, exactly as pyosg-khronos-viewer.py does; otherwise GLSL may
	// assign osg_Tangent to another generic attribute and normal mapping reads a default value.
	shader::configureProgram(*prog);

	prog->setName("osgGLTF_PBRIBLScene");
	prog->addShader(new osg::Shader(osg::Shader::VERTEX, detail::FULL_PBR_VERTEX_SHADER));
	prog->addShader(new osg::Shader(
		osg::Shader::FRAGMENT,
		resolveShaderLibs(detail::FULL_PBR_FRAGMENT_SHADER_SRC)
	));

	// resolveShaderLibs() expands the osgx snippet pragmas but preserves OSG's
	// import_defines pragma. Let OSG assemble/cache the diagnostic shader variant
	// from render state so it can place the generated define safely after #version.
	if(diagnostics) ss->setDefine("OSGX_PBRIBL_DIAGNOSTICS");

	ss->setAttributeAndModes(prog, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setTextureAttributeAndModes(5, environment.envMap, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(6, environment.brdfLUT, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(7, environment.diffuseEnv, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("envMap", 5));
	ss->addUniform(new osg::Uniform("brdfLUT", 6));
	ss->addUniform(new osg::Uniform("diffuseEnv", 7));
	ss->addUniform(new osg::Uniform("iblIntensity", iblIntensity));
	ss->addUniform(new osg::Uniform("emissiveFactor", osg::Vec3(1.0f, 1.0f, 1.0f)));
	ss->addUniform(new osg::Uniform("iblAxisX", environment.iblAxisX));
	ss->addUniform(new osg::Uniform("iblAxisY", environment.iblAxisY));
	ss->addUniform(new osg::Uniform("iblAxisZ", environment.iblAxisZ));

	// osgGLTF's Material helper binds the actual baseColor/normal/orm/emissive Texture2Ds to units
	// 0-3 per geometry, but deliberately stays shader-agnostic
	// and never sets the sampler *uniforms* that tell osgGLTF_textures which unit is which --
	// that's the shader glue's job (see MATERIAL_INPUTS's "unit N" comments above). Without this,
	// every sampler in the GLTFTextures struct silently defaults to unit 0 per the GLSL spec, so
	// normal/orm/emissive all end up reading the baseColor texture instead - corrupted shading
	// normals, scrambled roughness/metallic, and the whole baseColor image re-added as fake
	// "emissive" light. Same fix pyosg-khronos-viewer.py applies via its own uniforms.update(...).
	shader::configureStateSet(*ss);

	if(diagnostics) {
		pis.debugMode = new osg::Uniform("debugMode", 0);
		pis.disableNormalMap = new osg::Uniform("disableNormalMap", 0);
		pis.disableRoughnessMap = new osg::Uniform("disableRoughnessMap", 0);
		pis.disableSpecularAA = new osg::Uniform("disableSpecularAA", 0);

		ss->addUniform(pis.debugMode);
		ss->addUniform(pis.disableNormalMap);
		ss->addUniform(pis.disableRoughnessMap);
		ss->addUniform(pis.disableSpecularAA);
	}

	ss->setMode(GL_TEXTURE_CUBE_MAP_SEAMLESS, osg::StateAttribute::ON);

	return pis;
}

}
