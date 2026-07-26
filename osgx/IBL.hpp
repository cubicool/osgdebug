#pragma once

#include "Shader.hpp"
#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/GL>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/NodeCallback>
#include <osg/NodeVisitor>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osgDB/ReadFile>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// Signals that the final ordered pass in a frame-driven bake has completed. This means the result
// is ready for later GPU sampling in that render context; CPU readback remains a separate,
// explicitly synchronizing operation.
class BakeCompletion: public osg::Camera::DrawCallback {
public:
	void operator()(osg::RenderInfo&) const override {
		_done.store(true, std::memory_order_release);
	}

	bool done() const {
		return _done.load(std::memory_order_acquire);
	}

	void reset() {
		_done.store(false, std::memory_order_release);
	}

private:
	mutable std::atomic<bool> _done{false};
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

// Correlated Smith visibility, written exactly as the validated Python reference viewer's
// `smith(nv, nl, r)`. This is the visibility term already divided by 4*NdotL*NdotV for the
// split-sum integral; it is not interchangeable with a pair of Schlick G terms.
float smithVisibility(float NdotV, float NdotL, float roughness) {
	float alpha2 = pow(roughness, 4.0);
	float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2);
	float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2);

	return 0.5 / (gv + gl);
}

void main() {
	float NdotV = max(vUV.x, 1e-4);
	float roughness = vUV.y;
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

	float scale = 0.0, bias = 0.0;
	const uint SAMPLES = 512u;

	for(uint i = 0u; i < SAMPLES; i++) {
		vec2 Xi = hammersley(i, SAMPLES);
		vec3 H = importanceSampleGGX(Xi, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);

		if(NdotL > 0.0) {
			float G_vis = smithVisibility(NdotV, NdotL, roughness) * VdotH * NdotL / NdotH;
			float Fc = pow(1.0 - VdotH, 5.0);

			scale += (1.0 - Fc) * G_vis;
			bias += Fc * G_vis;
		}
	}

	fragColor = vec4(4.0 * scale / float(SAMPLES), 4.0 * bias / float(SAMPLES), 0.0, 1.0);
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
//
// Each shCoeffs[i] is A_l * L_lm (the cosine-lobe weight A_l already baked in by computeSH()'s
// projection - see its own comment). Reconstructing irradiance requires multiplying back in the
// SAME per-band Y_lm(n) normalization constant used during projection (0.282095/0.488603/
// 1.092548/0.315392/0.546274 below) - omitting them (as a prior revision of this function did)
// isn't "SH9 just being low-frequency," it's a real scale bug: band 0 alone comes out ~3.5x too
// bright without its 0.282095 factor. Matches OpenSceneGraph.py/pyosg-lighting/09-ibl.py's own
// sh_irradiance() constant-for-constant.
inline constexpr const char* SH_IRRADIANCE = R"GLSL(
vec3 osgx_SHIrradiance(vec3 N, vec3 shCoeffs[9]) {
	return max(
		shCoeffs[0] * 0.282095
		+ shCoeffs[1] * (0.488603 * N.y) + shCoeffs[2] * (0.488603 * N.z) + shCoeffs[3] * (0.488603 * N.x)
		+ shCoeffs[4] * (1.092548 * N.x * N.y) + shCoeffs[5] * (1.092548 * N.y * N.z)
		+ shCoeffs[6] * (0.315392 * (3.0 * N.z * N.z - 1.0))
		+ shCoeffs[7] * (1.092548 * N.x * N.z) + shCoeffs[8] * (0.546274 * (N.x * N.x - N.y * N.y)),
		vec3(0.0)
	);
}
)GLSL";

// ------------------------------------------------------------------------------------------------
// Baked Lambertian (cosine-weighted Monte Carlo) diffuse irradiance cubemap
//
// An alternative to SH9 above: a real per-texel convolution instead of 9 coefficients, so it
// keeps directional detail SH9's low-frequency basis can wash out in high-contrast environments.
// This is what the official Khronos glTF-Sample-Viewer itself bakes for diffuse IBL. Ported
// directly from OpenSceneGraph.py/examples/pyosg-khronos-viewer.py's make_lambertian_environment()
// -- same Hammersley/radical-inverse sequence, same per-face tangent-frame construction, same
// GL-cube-face -> Z-up-world axis convention IBL_SPECULAR's own R_gl swap already uses (so
// LAMBERTIAN_IRRADIANCE below applies the identical remap before sampling). Confirmed
// pixel-parity against github.khronos.org/glTF-Sample-Viewer-Release/ via that Python viewer,
// 2026-07-22 -- this is a mechanical C++ port of an already-validated algorithm, not a new design.
//
// Lives alongside SH9, not in place of it -- SH9 stays the right call for "cheap ambient, zero
// fuss"; reach for this when matching a real reference renderer (or high-contrast environments)
// actually matters. osgGLTF's optional PBR renderer uses this exclusively for Khronos parity.
// ------------------------------------------------------------------------------------------------

namespace detail {

inline double radicalInverseVdC(std::uint32_t bits) {
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
	bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
	bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
	bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);

	return double(bits) * 2.3283064365386963e-10;
}

// Unnormalized direction to a texel center on a GL-convention cube face -- face index/formula
// order matches osg::TextureCubeMap's POSITIVE_X/NEGATIVE_X/POSITIVE_Y/NEGATIVE_Y/POSITIVE_Z/
// NEGATIVE_Z, same as cube_directions() in the Python original.
inline osg::Vec3f cubeFaceDirection(int face, float s, float t) {
	switch(face) {
		case 0: return osg::Vec3f(1.0f, -t, -s);
		case 1: return osg::Vec3f(-1.0f, -t, s);
		case 2: return osg::Vec3f(s, 1.0f, t);
		case 3: return osg::Vec3f(s, -1.0f, -t);
		case 4: return osg::Vec3f(s, -t, 1.0f);
		default: return osg::Vec3f(-s, -t, -1.0f);
	}
}

// Read-only view of a GL_RGB/GL_FLOAT osg::Image, with the row stride pre-derived once. Passing
// this instead of an osg::Image* is the actual fix for the hot-loop cost sampleEquirect() below
// used to pay: osg::Image::data(x,y) LOOKS like a cheap header-inlined pointer computation, but it
// internally calls getPixelSizeInBits()/getRowStepInBytes(), which call
// computePixelSizeInBits()/computeRowWidthInBytes() - both declared `static` in the header but
// DEFINED out-of-line in libosg's Image.cpp, each running its own multi-case switch on
// pixelFormat/dataType/packing. Those values never change for a given image, so deriving them
// once here (instead of ~25 million times, once per bilinear tap per sample) is the difference
// that actually matters - not just swapping which OSG accessor gets called.
struct EquirectView {
	const float* data;
	int width, height;
	int rowStrideFloats; // == getRowStepInBytes() / sizeof(float); may exceed width*3 if padded

	explicit EquirectView(const osg::Image* img):
		data(reinterpret_cast<const float*>(img->data())),
		width(img->s()),
		height(img->t()),
		rowStrideFloats(static_cast<int>(img->getRowStepInBytes() / sizeof(float)))
	{
	}
};

// Bilinear sample of an equirectangular HDR image along a world-space (Z-up) direction -- same
// theta/phi convention computeSH() uses per-texel, but continuous, since Monte Carlo sample
// directions don't land on exact source pixels. Ported from sample_hdr() in the Python original.
// Requires `view` to wrap a real HDR-loaded (GL_RGB/GL_FLOAT) osg::Image, same requirement
// computeSH() already documents for its own input -- exactly what osgDB's own HDR (Radiance)
// reader always produces (see ReaderWriterHDR.cpp).
inline osg::Vec3f sampleEquirect(const EquirectView& view, const osg::Vec3f& dir) {
	const int W = view.width;
	const int H = view.height;

	double phi = std::atan2(double(dir.y()), double(dir.x())) / (2.0 * osg::PI);

	phi -= std::floor(phi); // wrap to [0, 1)

	const double theta = std::acos(std::clamp(double(dir.z()), -1.0, 1.0)) / osg::PI;
	const double x = phi * double(W) - 0.5;
	const double y = std::clamp(theta * double(H) - 0.5, 0.0, double(H - 1));
	const double fx = x - std::floor(x);
	const double fy = y - std::floor(y);

	int x0 = int(std::floor(x)) % W;

	if(x0 < 0) x0 += W;

	const int x1 = (x0 + 1) % W;
	const int y0 = int(std::floor(y));
	const int y1 = std::min(y0 + 1, H - 1);

	// OpenCV (the Python reference) addresses the Radiance panorama top-to-bottom. OSG images
	// address rows bottom-to-top; ReaderWriterHDR preserves that GL/OSG orientation when it loads
	// the file. Convert the sampled top-origin y coordinates back to OSG's row order here.
	const std::size_t row0Index = std::size_t(H - 1 - y0);
	const std::size_t row1Index = std::size_t(H - 1 - y1);
	const float* row0 = view.data + row0Index * std::size_t(view.rowStrideFloats);
	const float* row1 = view.data + row1Index * std::size_t(view.rowStrideFloats);
	const float* p00 = row0 + std::size_t(x0) * 3;
	const float* p10 = row0 + std::size_t(x1) * 3;
	const float* p01 = row1 + std::size_t(x0) * 3;
	const float* p11 = row1 + std::size_t(x1) * 3;
	const osg::Vec3f c00(p00[0], p00[1], p00[2]);
	const osg::Vec3f c10(p10[0], p10[1], p10[2]);
	const osg::Vec3f c01(p01[0], p01[1], p01[2]);
	const osg::Vec3f c11(p11[0], p11[1], p11[2]);
	const osg::Vec3f a = c00 * float(1.0 - fx) + c10 * float(fx);
	const osg::Vec3f b = c01 * float(1.0 - fx) + c11 * float(fx);

	return a * float(1.0 - fy) + b * float(fy);
}

}

// O(6 * size * size * samples) bilinear HDR samples -- meant to run once at startup (or once per
// environment swap), same contract as computeSH(). `hdrImg` must be a real HDR-loaded osg::Image
// (not LDR-clamped), same requirement as computeSH().
inline osg::ref_ptr<osg::TextureCubeMap> computeLambertianCubeMap(
	const osg::Image* hdrImg,
	int size = 64,
	int samples = 256
) {
	auto cube = make_ref<osg::TextureCubeMap>();

	static constexpr osg::TextureCubeMap::Face FACES[6] = {
		osg::TextureCubeMap::POSITIVE_X, osg::TextureCubeMap::NEGATIVE_X,
		osg::TextureCubeMap::POSITIVE_Y, osg::TextureCubeMap::NEGATIVE_Y,
		osg::TextureCubeMap::POSITIVE_Z, osg::TextureCubeMap::NEGATIVE_Z
	};

	// The tangent-space sample direction for sample `i` depends only on `i`/`samples` - not on the
	// texel or face it'll be transformed into - so it's computed exactly `samples` times here,
	// once, instead of redone from scratch (radicalInverseVdC() + sin/cos/sqrt) inside the
	// per-texel loop below (which would repeat identical work 6*size*size times over, unused
	// libm/bit-twiddling work that dwarfed the actual per-texel HDR sampling). Matches
	// make_lambertian_environment()'s own `sequence`/`phi`/`local` precomputed once up top.
	std::vector<osg::Vec3f> localSamples(static_cast<std::size_t>(samples));
	const detail::EquirectView hdrView(hdrImg);

	for(int i = 0; i < samples; i++) {
		const double xi2 = detail::radicalInverseVdC(static_cast<std::uint32_t>(i));
		const double phi = 2.0 * osg::PI * double(i) / double(samples);
		const double r = std::sqrt(xi2);

		localSamples[static_cast<std::size_t>(i)] = osg::Vec3f(
			float(r * std::cos(phi)),
			float(r * std::sin(phi)),
			float(std::sqrt(std::max(0.0, 1.0 - xi2)))
		);
	}

	for(int face = 0; face < 6; face++) {
		auto image = make_ref<osg::Image>();

		image->allocateImage(size, size, 1, GL_RGB, GL_FLOAT);

		for(int y = 0; y < size; y++) {
			const float t = (float(y) + 0.5f) * (2.0f / float(size)) - 1.0f;

			for(int x = 0; x < size; x++) {
				const float s = (float(x) + 0.5f) * (2.0f / float(size)) - 1.0f;

				osg::Vec3f g = detail::cubeFaceDirection(face, s, t);

				g.normalize();

				// GL-cube-face local direction -> Z-up world direction (matches IBL_SPECULAR's
				// own R_gl swap, applied in reverse).
				const osg::Vec3f n(g.x(), -g.z(), g.y());
				const osg::Vec3f up = std::abs(n.z()) > 0.99f
					? osg::Vec3f(0.0f, 1.0f, 0.0f)
					: osg::Vec3f(0.0f, 0.0f, 1.0f)
				;

				osg::Vec3f T = up ^ n;

				T.normalize();

				const osg::Vec3f B = n ^ T;

				osg::Vec3d accum(0.0, 0.0, 0.0);

				for(int i = 0; i < samples; i++) {
					const osg::Vec3f& local = localSamples[static_cast<std::size_t>(i)];
					const osg::Vec3f dir = T * local.x() + B * local.y() + n * local.z();
					const osg::Vec3f c = detail::sampleEquirect(hdrView, dir);

					accum += osg::Vec3d(double(c.x()), double(c.y()), double(c.z()));
				}

				accum /= double(samples);

				auto* px = reinterpret_cast<float*>(image->data(
					static_cast<unsigned int>(x),
					static_cast<unsigned int>(y)
				));

				px[0] = float(accum.x());
				px[1] = float(accum.y());
				px[2] = float(accum.z());
			}
		}

		cube->setImage(FACES[face], image);
	}

	cube->setUseHardwareMipMapGeneration(false);

	return cube;
}

// GLSL evaluation of a baked Lambertian cubemap at world-space normal N -- applies the same
// GL-cube-face axis remap IBL_SPECULAR's R_gl uses, so `diffuseEnv` must be sampled with a plain
// `samplerCube` bound to a cubemap baked by computeLambertianCubeMap() above (or an equivalent
// Y-up-convention bake).
inline constexpr const char* LAMBERTIAN_IRRADIANCE = R"GLSL(
vec3 osgx_LambertianIrradiance(vec3 N, samplerCube diffuseEnv) {
	vec3 N_gl = vec3(N.x, N.z, -N.y);

	return texture(diffuseEnv, N_gl).rgb;
}
)GLSL";

// Flat two-color "sky above / ground below" ambient term -- no cubemap, BRDF LUT, or SH bake
// needed, so a shader can have *some* ambient response with zero asset loading. This is the
// fallback path 09-ibl.py's evaluateIBL() takes when iblEnabled == 0; pulled out standalone here
// since a quick REPL/demo shader frequently wants exactly this and nothing else -- reach for the
// real SH_IRRADIANCE/IBL_SPECULAR pair above once an actual environment is worth loading.
inline constexpr const char* HEMISPHERE_AMBIENT = R"GLSL(
vec3 osgx_HemisphereAmbient(vec3 N, vec3 up, vec3 albedo, float ao, vec3 skyColor, vec3 groundColor) {
	float hemi = dot(N, up) * 0.5 + 0.5;

	return mix(groundColor, skyColor, hemi) * albedo * ao;
}
)GLSL";

}


namespace ibl {

inline void registerShaderLibs() {
	static constexpr ShaderLib libs[] = {
		{"SH_IRRADIANCE", "osgx_SHIrradiance", ibl::SH_IRRADIANCE},
		{"LAMBERTIAN_IRRADIANCE", "osgx_LambertianIrradiance", ibl::LAMBERTIAN_IRRADIANCE},
		{"HEMISPHERE_AMBIENT", "osgx_HemisphereAmbient", ibl::HEMISPHERE_AMBIENT}
	};
	::osgx::registerShaderLibs("osgx::ibl", libs);
}

}

}
