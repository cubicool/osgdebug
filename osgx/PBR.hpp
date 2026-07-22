#pragma once

#include "Shader.hpp"

namespace osgx {

// ================================================================================================
// PBR / IBL
//
// Two namespaces, kept deliberately separate:
//
// osgx::pbr -- the BRDF math itself (GGX distribution, Schlick Fresnel, Smith geometry term).
// Independent of where the incoming light comes from: the same terms feed a direct point-light
// loop or an IBL environment term. Plain GLSL snippet constants, not full shaders or hooked
// shader objects -- see the namespace-level comment below for why.
//
// osgx::ibl -- the environment-as-light-source pipeline: a prefiltered specular cubemap plus a
// split-sum BRDF LUT (Karis 2013), and eventually SH-9 diffuse irradiance. Calls into osgx::pbr
// for its Fresnel term.
//
// Ported from the STATIC path of OpenSceneGraph.py/examples/pyosg-lighting/09-ibl.py: a
// pre-baked .ktx2 prefiltered cubemap loaded once, plus a one-shot BRDF LUT bake. Deliberately
// does NOT include 10-dynamicprobes.py's live GPU re-bake -- out of scope here.
// ================================================================================================

namespace pbr {

// GLSL function-body snippets, not full shaders -- concatenate the ones you need into a
// consuming fragment shader (same mechanism osgSlug's SHADER_LIB_FRAGMENT uses: paste the
// source in, above main()). All function names carry the osgx_ prefix to avoid collisions
// with whatever else is in the consuming shader.
//
// Contract: these assume `const float PI = 3.14159265359;` is already in scope. Not bundled
// here, since plenty of consuming shaders already define PI themselves and a duplicate
// `const float PI` is a compile error, not a harmless redefinition -- the caller adds it once.

// GGX/Trowbridge-Reitz normal distribution term (D). NdotH and roughness in [0,1];
// `roughness * roughness` is the standard Disney/Karis alpha remap.
inline constexpr const char* D_GGX = R"GLSL(
float osgx_D_GGX(float NdotH, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}
)GLSL";

// Schlick-GGX geometry term for a single direction (view OR light). Combine both via
// osgx_G_Smith (G_SMITH below) for the full geometric attenuation term.
inline constexpr const char* G_SCHLICK = R"GLSL(
float osgx_G_Schlick(float NdotX, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotX / (NdotX * (1.0 - k) + k);
}
)GLSL";

// Smith's method: visible geometric attenuation = product of the view-side and light-side
// Schlick-GGX terms. Requires osgx_G_Schlick (G_SCHLICK) already in scope.
inline constexpr const char* G_SMITH = R"GLSL(
float osgx_G_Smith(float NdotV, float NdotL, float roughness) {
	return osgx_G_Schlick(NdotV, roughness) * osgx_G_Schlick(NdotL, roughness);
}
)GLSL";

// Fresnel-Schlick: reflectance rises toward white (dielectrics) or the material's own tint
// (metals, via F0 = mix(vec3(0.04), albedo, metallic)) at grazing angles. For direct lights.
inline constexpr const char* F_SCHLICK = R"GLSL(
vec3 osgx_F_Schlick(float cosTheta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
)GLSL";

// Roughness-aware Fresnel (Lagarde) -- for IBL specular, so a rough surface's Fresnel rim
// doesn't stay mirror-sharp the way plain F_Schlick would. Direct lights use F_SCHLICK instead.
inline constexpr const char* F_SCHLICK_ROUGHNESS = R"GLSL(
vec3 osgx_F_Schlick_roughness(float cosTheta, vec3 F0, float roughness) {
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
)GLSL";

// All five snippets, concatenated in dependency order (G_SMITH calls osgx_G_Schlick, so
// G_SCHLICK must precede it). Convenience for callers that want the whole BRDF toolkit;
// reach for the individual constants instead if only part of it is needed.
inline std::string snippets() {
	return std::string(D_GGX) + G_SCHLICK + G_SMITH + F_SCHLICK + F_SCHLICK_ROUGHNESS;
}

// Per-light Cook-Torrance specular contribution (direct lighting), already multiplied by NdotL --
// caller multiplies by the light's own radiance (color * intensity/distance^2 or similar) and
// accumulates. Requires D_GGX/G_SCHLICK/G_SMITH/F_SCHLICK already in scope (include snippets()
// or those four individually before this one).
inline constexpr const char* DIRECT_SPECULAR = R"GLSL(
vec3 osgx_DirectSpecular(vec3 N, vec3 V, vec3 L, float NdotV, float roughness, vec3 F0) {
	float NdotL = max(dot(N, L), 0.0);

	if(NdotL <= 0.0) return vec3(0.0);

	vec3 H = normalize(L + V);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float D = osgx_D_GGX(NdotH, roughness);
	float G = osgx_G_Smith(NdotV, NdotL, roughness);
	vec3 F = osgx_F_Schlick(HdotV, F0);

	return (D * G * F * NdotL) / max(4.0 * NdotV * NdotL, 0.0001);
}
)GLSL";

// Multi-scattering energy-compensated Fresnel (Fdez-Aguera 2019, "A Multiple-Scattering
// Microfacet Model for Real-Time Image-based Lighting"), the same formula the official Khronos
// glTF-Sample-Viewer uses (ported from OpenSceneGraph.py/examples/pyosg-khronos-viewer.py's
// fresnel(), which was itself written to match that viewer's IBL.glsl exactly -- confirmed
// pixel-parity against github.khronos.org/glTF-Sample-Viewer-Release/ on 2026-07-22).
//
// The classic single-scatter split-sum approximation (Karis 2013 -- what IBL_SPECULAR below
// used before this was added) loses energy at higher roughness because it only accounts for
// light bouncing off the microfacet surface once; this adds back an estimate of what multiple
// internal bounces would have contributed, using the same split-sum LUT (ab.x/ab.y) the
// single-scatter term already samples -- no extra texture reads, just more math on the same
// two numbers. Most visible on rough metals/dielectrics, which the single-scatter version
// renders measurably too dark/desaturated.
inline constexpr const char* F_MULTISCATTER = R"GLSL(
vec3 osgx_F_MultiScatter(vec3 N, vec3 V, float roughness, vec3 F0, sampler2D brdfLUT) {
	float NdotV = max(dot(N, V), 0.0);
	vec2 ab = texture(brdfLUT, clamp(vec2(NdotV, roughness), 0.0, 1.0)).rg;

	// Single-scatter Fresnel (Schlick, roughness-aware) and its split-sum-combined result.
	vec3 Fss = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
	vec3 FssCombined = Fss * ab.x + ab.y;

	// Energy lost to single-scatter, and the average Fresnel across all angles -- both from
	// Fdez-Aguera's derivation; feeds a geometric-series estimate of the multi-bounce term.
	float Ems = 1.0 - (ab.x + ab.y);
	vec3 Favg = F0 + (1.0 - F0) / 21.0;

	return FssCombined + Ems * FssCombined * Favg / (1.0 - Favg * Ems);
}
)GLSL";

// Split-sum IBL specular: samples the prefiltered cubemap along the reflection vector and
// combines with the baked BRDF LUT via the multi-scatter energy-compensated Fresnel above (not
// the plain Karis 2013 single-scatter combine this used to be). Handles the OSG (Z-up) ->
// baked-cubemap (Y-up) face remap internally. Requires F_MULTISCATTER already in scope.
inline constexpr const char* IBL_SPECULAR = R"GLSL(
vec3 osgx_IBLSpecular(
	vec3 N,
	vec3 V,
	vec3 F0,
	float roughness,
	samplerCube envMap,
	sampler2D brdfLUT,
	float envMaxMip
) {
	vec3 R = reflect(-V, N);

	// OSG world space is Z-up; the baked cubemap's faces are Y-up -- without this remap we'd
	// sample a direction that doesn't correspond to R at all.
	vec3 R_gl = vec3(R.x, R.z, -R.y);

	vec3 prefilt = textureLod(envMap, R_gl, roughness * envMaxMip).rgb;
	vec3 F = osgx_F_MultiScatter(N, V, roughness, F0, brdfLUT);

	return prefilt * F;
}
)GLSL";

// Khronos PBR Neutral tonemap -- hue-preserving (no ACES orange shift), for compressing HDR
// specular (routinely > 1.0 off a near-mirror surface under a bright environment) into LDR
// without hard-clipping to solid white. Ported verbatim from 09-ibl.py's tonemapPBRNeutral().
// Caller still applies its own gamma afterward if not rendering to an sRGB framebuffer.
inline constexpr const char* TONEMAP_PBR_NEUTRAL = R"GLSL(
vec3 osgx_TonemapPBRNeutral(vec3 color) {
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if(peak >= startCompression) {
		float d = 1.0 - startCompression;
		float newPeak = 1.0 - d * d / (peak + d - startCompression);
		color *= newPeak / peak;
		float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
		color = mix(color, vec3(newPeak), g);
	}
	return clamp(color, 0.0, 1.0);
}
)GLSL";

// Animates a handful of point lights orbiting a center point, writing world-space
// position+intensity into a vec4 array uniform ("lightPosIntensity" by default) every update
// traversal -- the motion is what confirms N/V/specular are wired correctly rather than just a
// static flat-shaded color. Install as the update callback on whichever node the lit shape hangs
// from; `ss` must be the StateSet holding that uniform (array size >= orbits.size()).
//
// Reusable across any osgx::pbr-lit scene -- configure `center`/`orbits`/`intensity` per use
// instead of copying this callback into each consumer.
struct OrbitLightRig: public osg::NodeCallback {
	struct Orbit {
		float radius, height, speed, phase, intensity;
	};

	osg::ref_ptr<osg::StateSet> ss;
	osg::Vec3 center{0.0f, 0.0f, 0.0f};
	float intensity = 1.0f; // global scale, e.g. a --light-intensity CLI flag
	std::string uniformName = "lightPosIntensity";

	// Default rig: (orbit radius, height above center, angular speed, phase, per-orbit intensity).
	// Matches the original osgslug-pbr-ibl.cpp badge rig; override for a different look.
	std::vector<Orbit> orbits = {
		{0.55f, 0.70f, 0.50f, 0.0f, 1.00f},
		{0.70f, 0.90f, -0.33f, 2.1f, 0.75f},
		{0.45f, 0.50f, 0.80f, 4.2f, 0.50f},
	};

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		float t = nv->getFrameStamp() ? float(nv->getFrameStamp()->getSimulationTime()) : 0.0f;
		auto* lp = ss->getUniform(uniformName);

		for(size_t i = 0; i < orbits.size(); i++) {
			const auto& o = orbits[i];
			float a = t * o.speed + o.phase;

			lp->setElement(static_cast<unsigned int>(i), osg::Vec4(
				center.x() + std::cos(a) * o.radius,
				center.y() + std::sin(a) * o.radius,
				center.z() + o.height,
				o.intensity * intensity
			));
		}

		traverse(node, nv);
	}
};

}


namespace pbr {

inline void registerShaderLibs() {
	static constexpr ShaderLib libs[] = {
		{"D_GGX", "osgx_D_GGX", pbr::D_GGX},
		{"G_SCHLICK", "osgx_G_Schlick", pbr::G_SCHLICK},
		{"G_SMITH", "osgx_G_Smith", pbr::G_SMITH},
		{"F_SCHLICK", "osgx_F_Schlick", pbr::F_SCHLICK},
		{"F_SCHLICK_ROUGHNESS", "osgx_F_Schlick_roughness", pbr::F_SCHLICK_ROUGHNESS},
		{"DIRECT_SPECULAR", "osgx_DirectSpecular", pbr::DIRECT_SPECULAR},
		{"F_MULTISCATTER", "osgx_F_MultiScatter", pbr::F_MULTISCATTER},
		{"IBL_SPECULAR", "osgx_IBLSpecular", pbr::IBL_SPECULAR},
		{"TONEMAP_PBR_NEUTRAL", "osgx_TonemapPBRNeutral", pbr::TONEMAP_PBR_NEUTRAL}
	};
	::osgx::registerShaderLibs("osgx::pbr", libs);
}

}

}
