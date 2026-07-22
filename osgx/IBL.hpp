#pragma once

#include "Shader.hpp"

namespace osgx {

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


namespace ibl {

inline void registerShaderLibs() {
	static constexpr ShaderLib libs[] = {{"SH_IRRADIANCE", "osgx_SHIrradiance", ibl::SH_IRRADIANCE}};
	::osgx::registerShaderLibs("osgx::ibl", libs);
}

}

}
