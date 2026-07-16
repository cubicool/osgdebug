#pragma once

#include "Core.hpp"

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

// Split-sum IBL specular (Karis 2013): samples the prefiltered cubemap along the reflection
// vector and combines with the baked BRDF LUT. Handles the OSG (Z-up) -> baked-cubemap (Y-up)
// face remap internally. Self-contained -- does not require snippets() (the split-sum combine
// folds Fresnel into brdfLUT rather than calling F_SCHLICK_ROUGHNESS directly).
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
	float NdotV = max(dot(N, V), 0.0);

	// OSG world space is Z-up; the baked cubemap's faces are Y-up -- without this remap we'd
	// sample a direction that doesn't correspond to R at all.
	vec3 R_gl = vec3(R.x, R.z, -R.y);

	vec3 prefilt = textureLod(envMap, R_gl, roughness * envMaxMip).rgb;
	vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

	return prefilt * (F0 * brdf.x + brdf.y);
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

namespace ibl {

// Disables a node after its update callback has fired exactly once -- e.g. a PRE_RENDER bake
// camera that should render one frame at startup and then go idle. Call rebake() to re-arm it
// (render one more frame -- e.g. after swapping the bake's source data).
class RunOnceCallback: public osg::NodeCallback {
public:
	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		if(_done) node->setNodeMask(0);

		_done = true;

		traverse(node, nv);
	}

	void rebake(osg::Node* node) {
		node->setNodeMask(0xFFFFFFFF);
		_done = false;
	}

private:
	bool _done = false;
};

// Fullscreen NDC-quad vertex shader -- shared by any single-pass bake (BRDF LUT today; future
// bakes that need a rasterized pass can reuse it too).
inline constexpr const char* FULLSCREEN_VERT = R"GLSL(
#version 330 core
in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;
out vec2 vUV;
void main() {
	vUV = osg_MultiTexCoord0;
	gl_Position = vec4(osg_Vertex.xy, 0.0, 1.0);
}
)GLSL";

// Split-sum BRDF LUT bake (Karis 2013) -- environment-independent, so it only ever needs to
// bake once. R channel = Fresnel scale, G channel = Fresnel bias; sampled in the consuming
// shader as texture(brdfLUT, vec2(NdotV, roughness)).rg.
inline constexpr const char* BRDF_LUT_FRAG = R"GLSL(
#version 330 core
const float PI = 3.14159265359;
in vec2 vUV;
out vec4 fragColor;

float radicalInverseVdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
	return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, float roughness) {
	float a = roughness * roughness;
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float G_GGX_IBL(float NdotX, float roughness) {
	float k = (roughness * roughness) / 2.0;
	return NdotX / (NdotX * (1.0 - k) + k);
}

void main() {
	float NdotV = max(vUV.x, 1e-4);
	float roughness = vUV.y;
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

	float scale = 0.0, bias = 0.0;
	const uint SAMPLES = 1024u;

	for(uint i = 0u; i < SAMPLES; i++) {
		vec2 Xi = hammersley(i, SAMPLES);
		vec3 H = importanceSampleGGX(Xi, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);

		if(NdotL > 0.0) {
			float G = G_GGX_IBL(NdotV, roughness) * G_GGX_IBL(NdotL, roughness);
			float G_vis = G * VdotH / max(NdotH * NdotV, 0.001);
			float Fc = pow(1.0 - VdotH, 5.0);

			scale += (1.0 - Fc) * G_vis;
			bias += Fc * G_vis;
		}
	}

	fragColor = vec4(scale / float(SAMPLES), bias / float(SAMPLES), 0.0, 1.0);
}
)GLSL";

// Loads a pre-baked GGX-prefiltered cubemap from a .ktx2 file (see osgGLTF's
// ReaderWriterKTX2.cpp for the plugin that makes this format readable -- must be registered
// with osgDB, same as any other reader/writer plugin). The KTX2 is expected to carry its own
// hand-baked mip chain, one level per roughness step -- hardware mipmap generation is disabled
// so OSG doesn't overwrite it.
//
// Returns nullptr (and logs via OSG_WARN) if the path doesn't load, or doesn't load as a
// TextureCubeMap.
inline osg::ref_ptr<osg::TextureCubeMap> loadPrefilterCubemap(const std::string& path) {
	osg::ref_ptr<osg::Object> obj = osgDB::readRefObjectFile(path);
	auto* cube = dynamic_cast<osg::TextureCubeMap*>(obj.get());

	if(!cube) {
		OSG_WARN <<
			"osgx::ibl::loadPrefilterCubemap: " << path <<
			" did not load as a TextureCubeMap" << std::endl
		;

		return nullptr;
	}

	cube->setUseHardwareMipMapGeneration(false);

	return cube;
}

// Creates a PRE_RENDER FBO camera that bakes the split-sum BRDF LUT into `lut` exactly once
// (via RunOnceCallback -- installed as the camera's update callback). `lut` is configured
// in-place (size/format/filters), matching the out-param convention used by the two-argument
// makePickCamera() overload above. The caller is responsible for:
//
// - adding the returned camera as a child of the scene graph (anywhere -- it's ABSOLUTE_RF)
// - NOT expecting it to re-bake on its own: the LUT's only inputs are NdotV and roughness,
//   both baked into the UV axes, so a static environment never needs a second bake. Call
//   rebake() on the camera's RunOnceCallback (via getUpdateCallback()) if that ever changes.
inline osg::ref_ptr<osg::Camera> makeBRDFLUTCamera(int lutSize, osg::Texture2D* lut) {
	lut->setTextureSize(lutSize, lutSize);
	lut->setInternalFormat(GL_RGBA);
	lut->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	lut->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	lut->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	lut->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	auto prog = make_ref<osg::Program>();

	prog->setName("osgx_ibl_brdfLutBake");
	prog->addShader(new osg::Shader(osg::Shader::VERTEX, FULLSCREEN_VERT));
	prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, BRDF_LUT_FRAG));

	auto quad = osg::createTexturedQuadGeometry(
		osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0)
	);

	auto geode = make_ref<osg::Geode>();

	geode->addDrawable(quad);

	auto cam = make_ref<osg::Camera>();

	cam->setName("osgx_ibl_BRDFLUTBake");
	cam->setRenderOrder(osg::Camera::PRE_RENDER);
	cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	cam->setClearMask(GL_COLOR_BUFFER_BIT);
	cam->setViewport(0, 0, lutSize, lutSize);
	cam->setProjectionMatrix(osg::Matrix::identity());
	cam->setViewMatrix(osg::Matrix::identity());
	cam->attach(osg::Camera::COLOR_BUFFER0, lut);
	cam->getOrCreateStateSet()->setAttributeAndModes(prog, osg::StateAttribute::ON);
	cam->addChild(geode);
	cam->setUpdateCallback(new RunOnceCallback());

	return cam;
}

// ------------------------------------------------------------------------------------------------
// SH-9 diffuse irradiance
//
// L0-L2 spherical harmonics: 9 RGB coefficients standing in for the whole low-frequency diffuse
// environment -- much cheaper than sampling a cubemap per-pixel for diffuse, at the cost of only
// capturing broad/blurry lighting (which is all diffuse irradiance ever needs). Ported from
// 09-ibl.py's compute_sh() (projection) and sh_irradiance() (GLSL evaluation).
// ------------------------------------------------------------------------------------------------

struct SH9 {
	osg::Vec3f coeffs[9];
};

// Projects an equirectangular (2:1) HDR/LDR environment image onto SH9. Cosine-lobe A_l weights
// are baked in here so the GLSL evaluation (SH_IRRADIANCE below) is a plain dot-product sum.
//
// O(width*height) -- meant to run once at startup (or once per environment swap), not per frame.
// img's pixel format is read via osg::Image::getColor(), which returns true (unnormalized) float
// radiance for float-format images -- use a genuinely HDR-loaded osg::Image (e.g. a .hdr file via
// osgDB::readImageFile()), not an LDR-clamped one, or the diffuse term will be dim/wrong.
inline SH9 computeSH(const osg::Image* img) {
	SH9 sh;

	const auto W = static_cast<std::size_t>(std::max(img->s(), 0));
	const auto H = static_cast<std::size_t>(std::max(img->t(), 0));

	for(std::size_t y = 0; y < H; y++) {
		const double theta = (double(y) + 0.5) / double(H) * osg::PI;
		const double sinTheta = std::sin(theta);
		const double cosTheta = std::cos(theta);
		const double dOmega = sinTheta * (osg::PI / double(H)) * (2.0 * osg::PI / double(W));

		for(std::size_t x = 0; x < W; x++) {
			const double phi = (double(x) + 0.5) / double(W) * 2.0 * osg::PI;
			const double sx = sinTheta * std::cos(phi);
			const double sy = sinTheta * std::sin(phi);
			const double sz = cosTheta;

			const double Y[9] = {
				0.282095,
				0.488603 * sy,
				0.488603 * sz,
				0.488603 * sx,
				1.092548 * sx * sy,
				1.092548 * sy * sz,
				0.315392 * (3.0 * sz * sz - 1.0),
				1.092548 * sx * sz,
				0.546274 * (sx * sx - sy * sy)
			};

			const double A[9] = {
				osg::PI,
				2.0 * osg::PI / 3.0,
				2.0 * osg::PI / 3.0,
				2.0 * osg::PI / 3.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0,
				osg::PI / 4.0
			};

			const osg::Vec4f c = img->getColor(
				static_cast<unsigned int>(x),
				static_cast<unsigned int>(y)
			);

			for(int i = 0; i < 9; i++) {
				const double w = Y[i] * A[i] * dOmega;

				sh.coeffs[i] += osg::Vec3f(
					float(double(c.r()) * w),
					float(double(c.g()) * w),
					float(double(c.b()) * w)
				);
			}
		}
	}

	return sh;
}

// GLSL evaluation of an SH9 environment at world-space normal N. shCoeffs is a 9-element array
// uniform (or local) -- caller declares and binds it under whatever name fits their shader
// (e.g. `uniform vec3 iblSH[9];`), then calls osgx_SHIrradiance(N, iblSH).
inline constexpr const char* SH_IRRADIANCE = R"GLSL(
vec3 osgx_SHIrradiance(vec3 N, vec3 shCoeffs[9]) {
	return max(
		shCoeffs[0]
		+ shCoeffs[1] * N.y + shCoeffs[2] * N.z + shCoeffs[3] * N.x
		+ shCoeffs[4] * N.x * N.y + shCoeffs[5] * N.y * N.z
		+ shCoeffs[6] * (3.0 * N.z * N.z - 1.0)
		+ shCoeffs[7] * N.x * N.z + shCoeffs[8] * (N.x * N.x - N.y * N.y),
		vec3(0.0)
	);
}
)GLSL";

}

namespace detail {

struct ShaderLibEntry {
	std::string_view name;
	std::string_view glslName;
	std::string_view source;
};

inline std::string_view trim(std::string_view text) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c); };

	while(!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
		text.remove_prefix(1);
	}

	while(!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
		text.remove_suffix(1);
	}

	return text;
}

inline bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
	if(text.size() < prefix.size()) return false;

	return std::equal(prefix.begin(), prefix.end(), text.begin(), [](
		const char lhs,
		const char rhs
	) {
		return std::tolower(static_cast<unsigned char>(lhs)) ==
			std::tolower(static_cast<unsigned char>(rhs));
	});
}

inline std::string normalizeShaderLibName(std::string_view name) {
	name = trim(name);

	if(startsWithIgnoringCase(name, "osgx_")) name.remove_prefix(5);

	std::string normalized;

	normalized.reserve(name.size());

	for(const auto c : name) {
		if(std::isalnum(static_cast<unsigned char>(c))) {
			normalized.push_back(
				static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
			);
		}
	}

	return normalized;
}

inline bool shaderLibNameMatches(std::string_view requested, const ShaderLibEntry& entry) {
	const auto normalized = normalizeShaderLibName(requested);

	return normalized == normalizeShaderLibName(entry.name) ||
		normalized == normalizeShaderLibName(entry.glslName);
}

inline std::vector<std::string_view> splitShaderLibNames(std::string_view names) {
	if(const auto comment = names.find("//"); comment != std::string_view::npos) {
		names = names.substr(0, comment);
	}

	std::vector<std::string_view> tokens;

	for(std::string_view::size_type pos = 0; pos <= names.size();) {
		const auto comma = names.find(',', pos);
		const auto end = comma == std::string_view::npos ? names.size() : comma;
		const auto token = trim(names.substr(pos, end - pos));

		if(!token.empty()) tokens.push_back(token);
		if(comma == std::string_view::npos) break;

		pos = comma + 1;
	}

	return tokens;
}

inline const auto& pbrShaderLibs() {
	static const std::array<ShaderLibEntry, 8> libs = {{
		{"D_GGX", "osgx_D_GGX", pbr::D_GGX},
		{"G_SCHLICK", "osgx_G_Schlick", pbr::G_SCHLICK},
		{"G_SMITH", "osgx_G_Smith", pbr::G_SMITH},
		{"F_SCHLICK", "osgx_F_Schlick", pbr::F_SCHLICK},
		{"F_SCHLICK_ROUGHNESS", "osgx_F_Schlick_roughness", pbr::F_SCHLICK_ROUGHNESS},
		{"DIRECT_SPECULAR", "osgx_DirectSpecular", pbr::DIRECT_SPECULAR},
		{"IBL_SPECULAR", "osgx_IBLSpecular", pbr::IBL_SPECULAR},
		{"TONEMAP_PBR_NEUTRAL", "osgx_TonemapPBRNeutral", pbr::TONEMAP_PBR_NEUTRAL}
	}};

	return libs;
}

inline const auto& iblShaderLibs() {
	static const std::array<ShaderLibEntry, 1> libs = {{
		{"SH_IRRADIANCE", "osgx_SHIrradiance", ibl::SH_IRRADIANCE}
	}};

	return libs;
}

template<typename Libs>
std::string resolveShaderLibs(
	std::string_view namespaceName,
	std::string_view names,
	const Libs& libs
) {
	const auto tokens = splitShaderLibNames(names);

	if(tokens.empty()) {
		throw std::runtime_error(
			"osgx::resolveLibs: empty #pragma osgx::" + std::string(namespaceName)
		);
	}

	const auto includeAll = tokens.size() == 1 && trim(tokens.front()) == "*";
	std::string resolved;

	for(const auto& lib : libs) {
		const auto requested =
			includeAll ||
			std::any_of(tokens.begin(), tokens.end(), [&lib](auto token) {
				return shaderLibNameMatches(token, lib);
			})
		;

		if(requested) resolved += lib.source;
	}

	if(!includeAll) {
		for(const auto token : tokens) {
			const auto found = std::any_of(libs.begin(), libs.end(), [token](const auto& lib) {
				return shaderLibNameMatches(token, lib);
			});

			if(!found) {
				throw std::runtime_error(
					"osgx::resolveLibs: unknown #pragma osgx::" +
					std::string(namespaceName) + " lib '" + std::string(token) + "'"
				);
			}
		}
	}

	return resolved;
}

inline std::optional<std::string> resolveShaderLibPragma(std::string_view line) {
	auto rest = trim(line);

	if(!startsWithIgnoringCase(rest, "#pragma")) return std::nullopt;

	rest.remove_prefix(std::string_view("#pragma").size());
	rest = trim(rest);

	if(!startsWithIgnoringCase(rest, "osgx::")) return std::nullopt;

	rest.remove_prefix(std::string_view("osgx::").size());

	const auto nsEnd = std::find_if(rest.begin(), rest.end(), [](const char c) {
		return std::isspace(static_cast<unsigned char>(c));
	});
	const auto nsSize = static_cast<std::string_view::size_type>(std::distance(rest.begin(), nsEnd));
	const auto namespaceName = rest.substr(0, nsSize);
	const auto names = rest.substr(nsSize);

	if(startsWithIgnoringCase(namespaceName, "pbr") && namespaceName.size() == 3) {
		return resolveShaderLibs(namespaceName, names, pbrShaderLibs());
	}

	if(startsWithIgnoringCase(namespaceName, "ibl") && namespaceName.size() == 3) {
		return resolveShaderLibs(namespaceName, names, iblShaderLibs());
	}

	throw std::runtime_error(
		"osgx::resolveLibs: unknown #pragma osgx namespace '" +
		std::string(namespaceName) + "'"
	);
}

}

// Replaces line-oriented shader library pragmas with their GLSL source. Examples:
//
// #pragma osgx::pbr D_GGX,G_SCHLICK,G_SMITH
// #pragma osgx::pbr *
// #pragma osgx::ibl SH_IRRADIANCE
//
// Names are case-insensitive, and may use either the C++ catalog name (`F_SCHLICK`) or the
// GLSL function name (`osgx_F_Schlick`). `*` expands the whole namespace catalog in dependency
// order. Unknown osgx pragmas throw, so shader typos fail close to the source string.
inline std::string resolveLibs(std::string src) {
	std::string resolved;

	resolved.reserve(src.size());

	for(std::string::size_type pos = 0; pos < src.size();) {
		const auto lineEnd = src.find_first_of("\r\n", pos);
		const auto lineSize = lineEnd == std::string::npos ? src.size() - pos : lineEnd - pos;
		const std::string_view line(src.data() + pos, lineSize);

		if(auto replacement = detail::resolveShaderLibPragma(line)) resolved += *replacement;
		else resolved.append(line);

		if(lineEnd == std::string::npos) break;

		if(src[lineEnd] == '\r' && lineEnd + 1 < src.size() && src[lineEnd + 1] == '\n') {
			resolved.append("\r\n");
			pos = lineEnd + 2;
		}
		else {
			resolved.push_back(src[lineEnd]);
			pos = lineEnd + 1;
		}
	}

	return resolved;
}

}
