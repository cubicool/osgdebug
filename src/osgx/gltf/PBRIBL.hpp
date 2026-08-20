#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Group>
#include <osg/Node>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/Uniform>
#include <osg/Vec3>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "osgx/LambertianBake.hpp"
#include "osgx/Shadow.hpp"
#include "osgx/GBuffer.hpp"

// Forward declaration only -- keeps tinygltf's header out of this public header's include list.
// Only PBRIBL.cpp, which implements decodeIBLEnvironments(), needs the real definition.
namespace tinygltf { class Value; }

namespace osgx::gltf::pbribl {

// GLSL helpers that interpret the exact material interface declared by Shader.hpp. These are
// glTF-specific adapters over the renderer-independent snippets provided by osgx::pbr.
extern const char GET_MATERIAL[];
extern const char SHADING_NORMAL[];
extern const char EMISSIVE[];
extern const char ALPHA_COVERAGE[];

// Registers the osgx::gltf shader catalog used by `#pragma osgx::gltf ...`. Registration is
// idempotent.
void registerShaderLibs();

// Registers the generic osgx PBR/IBL catalogs plus glTF's catalogs, then expands them together.
// Keeping registration and resolution in this component avoids cross-shared-library registry
// assumptions for Python and plugin consumers.
std::string resolveShaderLibs(std::string_view source);

// Prepared IBL resources. `root`, when present, contains the PRE_RENDER passes that populate a
// generated BRDF LUT and/or cubemaps; add it to a rendered scene graph before using them.
// Fully pre-baked resources have no preparation root and can leave it null.
struct PBRIBLEnvironment {
	osg::ref_ptr<osg::Group> root;
	osg::ref_ptr<osg::Camera> lutCamera;
	osg::ref_ptr<osg::Group> diffuseBakeRoot;
	// Present only for the HDR-only path, whose specular cubemap is baked live.
	osg::ref_ptr<osg::Group> specularBakeRoot;
	osg::ref_ptr<osg::TextureCubeMap> envMap;
	osg::ref_ptr<osg::Texture2D> brdfLUT;
	osg::ref_ptr<osg::TextureCubeMap> diffuseEnv;
	// KTX/OpenGL cubemap lookup basis, expressed relative to the loader's Z-up world --
	// always exactly 3 (X/Y/Z row) vectors, one orthonormal basis, in the SAME Z-up space a
	// caller already reasons about (world-space N/V/R, no glTF/Y-up swizzle applied). Never
	// uploaded to the shader as-is -- see foldZUpToGLTFAxis() below.
	std::array<osg::Vec3, 3> iblAxis{
		osg::Vec3(0.0f, 0.0f, 1.0f),
		osg::Vec3(0.0f, 1.0f, 0.0f),
		osg::Vec3(-1.0f, 0.0f, 0.0f)
	};

	bool valid() const;
};

// Folds the fixed Z-up -> glTF/Y-up cubemap permutation (osgx_ZUpToGLTF() in the shader:
// vec3(d.x, d.z, -d.y)) into ONE `iblAxis` row, algebraically: composing OrientIBL's dot-product
// matrix (rows = iblAxis) with ZUpToGltf's fixed permutation matrix once here, on the CPU, instead
// of applying both transforms to every N/R per fragment on the GPU. Derivation: for row r,
// (ZUpToGltf then dot-with-r) == dot-with-(r.x, -r.z, r.y) for every input vector -- verified
// against the original two-step formula on both axis-aligned and general test vectors.
//
// createPBRIBLScene() calls this once per row when building the `iblAxis` uniform, so the shader's
// own osgx_OrientIBL(d) can be called directly on a raw Z-up N_world/R (no separate
// osgx_ZUpToGLTF() call at that site) and still land in the identical cubemap-lookup space.
// `environment.iblAxis` itself keeps its original Z-up meaning -- a caller overriding it for a
// custom convention still reasons in that space; only the uploaded uniform is pre-folded.
// osgx_ZUpToGLTF() remains a standalone shader function for its OTHER call sites (createPBRIBLScene()'s
// OSGX_PBRIBL_DIAGNOSTICS debug-normal visualizations), which have no iblAxis involved at all.
inline osg::Vec3 foldZUpToGLTFAxis(const osg::Vec3& row) {
	return osg::Vec3(row.x(), -row.z(), row.y());
}

// Fully dynamic path: bakes the GGX-prefiltered specular cubemap live, in memory, from `hdrPath`
// alone -- the same osgx::ibl::createGGXPrefilterScene() workflow osggltf-iblbake-gpu already
// wraps to write a KTX2 to disk, called directly instead of round-tripping through a file. Frame-
// driven like the existing diffuse/LUT bakes: envMap is a valid, bindable texture immediately, but
// its contents only become correct once specularBakeRoot's passes have actually run a few frames.
PBRIBLEnvironment preparePBRIBLEnvironment(const std::string& hdrPath, int lutSize=1024);

// One `environments[]` entry decoded from an `osgx_pbribl` glTF extension block (see
// ~/dev/osgdebug/TODO.md section 2b for the manifest schema this mirrors). Pure data: no textures,
// no I/O. `uri` is exactly what the manifest declared -- relative, resolving it (osgx::findDataFile
// et al.) and loading/baking the result is the caller's job.
struct IBLEnvironmentManifest {
	struct Resource {
		std::string uri;

		bool valid() const { return !uri.empty(); }
	};

	// Only `specular` carries bake-convention parameters today; see the GGXPrefilterOptions
	// discussion in osgdebug/TODO.md for why diffuse doesn't need equivalents yet.
	struct SpecularResource: Resource {
		int prefilterSize = 0;
		int lowestMipLevel = 0;
	};

	// Either a URI to a serialized LUT (for existing portable manifests), or the exact built-in
	// contract understood by this renderer. The latter uses osgx::ibl::sharedBRDFLUT(size).
	struct BRDFLUTResource: Resource {
		std::string builtin;
		int size = 1024;

		bool valid() const { return !uri.empty() || !builtin.empty(); }
	};

	SpecularResource specular;
	Resource diffuse;
	BRDFLUTResource brdfLUT;
};

// Decodes every `environments[]` entry out of an `osgx_pbribl` extension block. `extensionValue`
// is whatever `tinygltf::Model::extensions.at("osgx_pbribl")` returns -- tinygltf parses the root
// `extensions` block through the same generic path whether it came from a minimal standalone
// manifest document or a real asset's own embedded extension, so this one function covers both.
std::vector<IBLEnvironmentManifest> decodeIBLEnvironments(const tinygltf::Value& extensionValue);

// Static/pre-baked path: loads specular + diffuse cubemaps as KTX2. A URI BRDF LUT is loaded as a
// plain image; a recognized built-in BRDF LUT is shared and baked once per process/size. The latter
// supplies a preparation root on its first use. `manifest`'s relative URIs resolve against
// `baseDir` (normally the manifest document's own directory).
PBRIBLEnvironment loadPBRIBLEnvironment(const IBLEnvironmentManifest& manifest, const std::string& baseDir);

// Convenience overload: loads `manifestPath` as a glTF document -- a minimal standalone manifest or
// a real asset's own embedded osgx_pbribl block both work identically, see decodeIBLEnvironments()
// -- decodes its first declared environment, and resolves that environment's resources relative to
// the manifest file's own directory.
PBRIBLEnvironment loadPBRIBLEnvironment(const std::string& manifestPath);

struct PBRIBLScene {
	osg::ref_ptr<osg::Node> node;
	osg::ref_ptr<osg::Uniform> debugMode;
	osg::ref_ptr<osg::Uniform> disableNormalMap;
	osg::ref_ptr<osg::Uniform> disableRoughnessMap;
	osg::ref_ptr<osg::Uniform> disableSpecularAA;
	// Independent diffuse-irradiance/specular-reflection intensity knobs (not one shared
	// iblIntensity -- see evaluateIBL()'s own comment in PBRIBL.cpp for why they need to move
	// independently). Exposed as live osg::Uniform refs, same pattern as debugMode etc. above, so
	// a caller can tune -- or dial toward zero, e.g. to make punctual lights read more clearly --
	// after scene creation instead of only at construction time.
	osg::ref_ptr<osg::Uniform> iblDiffuseIntensity;
	osg::ref_ptr<osg::Uniform> iblSpecularIntensity;

	bool valid() const;
};

// Applies the glTF PBR/IBL renderer to a node using reusable prepared resources. `shadowMap`, when
// non-null, swaps in osgx::shadow::DIRECT_LIGHTING_HOOK_SHADOWED in place of
// osgx::pbr::DIRECT_LIGHTING_HOOK_DEFAULT and wires its depth texture + shadow uniforms onto
// `node`'s StateSet -- the caller still owns building the ShadowMap itself (its light direction
// has to match whatever the caller populates into osgx::pbr::LightSet at ShadowMap::casterIndex)
// and adding `shadowMap->camera` to the scene graph; this only handles the shader-side wiring.
// Default (nullptr) is unshadowed IBL+direct lighting, unchanged from before this parameter
// existed.
PBRIBLScene createPBRIBLScene(
	osg::Node* node,
	const PBRIBLEnvironment& environment,
	float iblDiffuseIntensity=1.0f,
	float iblSpecularIntensity=1.0f,
	bool diagnostics=false,
	const osgx::shadow::ShadowMap* shadowMap=nullptr,
	osg::Shader* tonemapHook=nullptr
);

// ================================================================================================
// Deferred split: createPBRIBLGeometryPass() + createPBRIBLLightingPass(), the two-camera
// counterpart to createPBRIBLScene()'s one-shader-does-everything shape above. Generalizes the
// G-buffer layout (gAlbedo/gNormal/gMaterial/gEmissive + depth) OpenSceneGraph.py's
// examples/pyosg-lighting/11-sketchfab.py hand-built and validated pixel-for-pixel against
// Sketchfab's own renderer for its deferred G-buffer + SSAO/bloom post-fx capstone -- the
// lighting pass runs the SAME evaluateIBL()/osgx_DirectLighting() logic
// createPBRIBLScene()'s monolithic shader does, just reading G-buffer textures instead of
// interpolated varyings. This is an architectural split, not new shader math.
// ================================================================================================

// Populated deferred G-buffer: `gbuffer.camera` is the PRE_RENDER geometry pass (add it to the
// scene graph); the typed texture refs below are exactly `gbuffer.colorTextures[0..4]`, broken
// out by name for readability at the call site. `normalTexture`/`positionTexture` are VIEW-space
// (matching the main camera whose real view matrix createPBRIBLLightingPass()'s fullscreen quad
// rotates world-space values from) -- not world-space, unlike createPBRIBLScene()'s
// eye-space-varyings-then-rotate-in-shader approach; the lighting pass performs that same
// rotation itself, once per pixel instead of once per vertex. `positionTexture` is real eye-space
// position written straight from the vertex shader -- NOT reconstructed from depth + an inverse
// projection matrix in the lighting pass, which turns out to be fundamentally unreliable here
// (see createPBRIBLLightingPass()'s own comment for why) -- `depthTexture` still exists for real
// GL depth-testing during the geometry pass (and for a caller's own debug/visualize use), it just
// isn't what the lighting pass reconstructs position from.
struct PBRIBLGBuffer {
	osgx::gbuffer::GBuffer gbuffer;
	osg::ref_ptr<osg::Texture2D> albedoTexture;   // rgb = albedo, a = ambient occlusion
	osg::ref_ptr<osg::Texture2D> normalTexture;   // rgb = view-space shading normal (RGB16F)
	osg::ref_ptr<osg::Texture2D> materialTexture; // r = roughness, g = metallic
	osg::ref_ptr<osg::Texture2D> emissiveTexture; // rgb = emissive (HDR), a = alpha coverage
	osg::ref_ptr<osg::Texture2D> positionTexture; // rgb = view-space position (RGBA32F)
	osg::ref_ptr<osg::Texture2D> depthTexture;

	bool valid() const;
};

// Writes material only -- no lighting, not even emissive add (that's still stored, just not
// combined with anything until the lighting pass). `node` becomes the geometry pass's child,
// same as the `node` a caller would otherwise hand to createPBRIBLScene() directly.
PBRIBLGBuffer createPBRIBLGeometryPass(osg::Node* node, int width, int height);

// Extra lighting-pass inputs, each an independent, optional seam rather than one monolithic
// flag blob:
// - `shadowMap` mirrors createPBRIBLScene()'s own parameter exactly (nullptr = unshadowed).
// - `aoTexture`, if set, is multiplied into the ambient term. The lighting pass does NOT bake
//   SSAO itself -- the kernel/sample-count/blur/noise-texture choices are too taste-dependent
//   to standardize into one osgx call; a caller builds its own SSAO pass (same shape
//   11-sketchfab.py's own hand-built one already proved out) and feeds the result in here.
// - `tonemap=false` leaves this pass's output as raw linear HDR (no tone curve, no gamma) -- for
//   a caller chaining bloom/exposure/etc. passes after this one instead of ending the pipeline at
//   this call. It strips the CALL (via OSGX_PBRIBL_NO_TONEMAP) but still attaches a DEFINITION of
//   osgx_Tonemap() -- the identity one, TONEMAP_HOOK_IDENTITY. Attaching no hook at all would make
//   the function's existence depend on a #define, and a define is not guaranteed to be present at
//   every compile: OSG's realize-time GLObjectsVisitor pre-compile pass sees an empty define
//   string and would link a call with no definition. See TONEMAP_HOOK_IDENTITY's comment in
//   PBR.hpp for the full mechanism.
//
//   Note these are two separate decisions on purpose, not one flag split in two: WHICH tone curve
//   runs is a hook swap (see `tonemapHook`), while WHETHER this pass emits display-referred or
//   linear output is a property of the pass's output contract, and gates the gamma encode as well
//   as the curve. Do not "simplify" by folding the gamma into the hook -- a custom hook author
//   would then have to remember to apply a transfer function too.
// - `tonemapHook`, if set, is used as THE definition of osgx_Tonemap() for this pass, in place of
//   either built-in. This is the seam for a custom tone curve (ACES, a look LUT, a filmic
//   response); it takes precedence over `tonemap`, since supplying a curve means you want it to
//   run. Compose it the same way any other osgx shader is composed -- a FRAGMENT osg::Shader whose
//   source defines osgx_Tonemap(vec3), optionally splicing library snippets in by name:
//
//       auto hook = new osg::Shader(osg::Shader::FRAGMENT, osgx::resolveShaderLibs(R"GLSL(
//           #version 460 core
//           #pragma osgx::pbr TONEMAP_ACES
//           vec3 osgx_Tonemap(vec3 color) { return osgx_TonemapACES(color); }
//       )GLSL"));
//
//   Attach a hook rather than adding a second shader that defines osgx_Tonemap() alongside the
//   built-in: GLSL permits exactly ONE body per function, so a competing definition is a link
//   error, not an override. This pass ALWAYS attaches exactly one definition (see
//   createPBRIBLLightingPass()'s own comment for why never-zero matters); `tonemapHook` chooses
//   WHICH, it does not add another.
struct PBRIBLLightingPassOptions {
	bool tonemap = true;
	// Custom osgx_Tonemap() definition; null uses the built-in `tonemap` selects. See the notes
	// above -- this REPLACES the built-in hook, it is not attached alongside one.
	osg::Shader* tonemapHook = nullptr;
	const osgx::shadow::ShadowMap* shadowMap = nullptr;
	osg::Texture2D* aoTexture = nullptr;
	bool diagnostics = false;
	// Where this pass draws. Left null (the default) it draws to whatever framebuffer the returned
	// camera ends up under -- POST_RENDER to the backbuffer for a caller that adds it straight to
	// the viewer, i.e. "the pipeline ends here", matching tonemap's own default of true.
	//
	// Set it, and the pass is BUILT as a PRE_RENDER/FBO camera targeting that texture, for callers
	// chaining further passes (bloom, exposure, a tonemap-comparison composite) that need this
	// pass's linear HDR result as a sampler input -- normally alongside tonemap=false, so the
	// chain tonemaps once at its own end rather than here.
	//
	// This exists so that re-targeting is a supported, tested option rather than something each
	// caller re-derives by mutating the returned camera. Hand-retargeting is easy to get subtly
	// wrong (the clear mask and the implicit depth attachment both matter, and getting them wrong
	// silently yields a flat, clear-colored texture -- see createPBRIBLLightingPass()'s own
	// GL_DEPTH_TEST comment), and every caller was reproducing that same guesswork independently.
	osg::Texture2D* colorTexture = nullptr;
	// Render order for the PRE_RENDER camera, honored only when colorTexture is set. Needs to sort
	// AFTER the geometry pass (and any shadow/SSAO pass feeding this one) and BEFORE whatever
	// consumes colorTexture.
	int renderOrderNum = 0;
};

struct PBRIBLLightingScene {
	osg::ref_ptr<osg::Node> node;
	osg::ref_ptr<osg::Uniform> iblDiffuseIntensity;
	osg::ref_ptr<osg::Uniform> iblSpecularIntensity;
	// Updated by updatePBRIBLLightingPass() -- see createPBRIBLLightingPass()'s own comment for
	// why the fullscreen quad's own ABSOLUTE_RF camera can't supply these automatically. No
	// projection-matrix uniform here (a first version had one, for reconstructing position from
	// depth) -- see PBRIBLGBuffer::positionTexture's comment for why that reconstruction was
	// dropped entirely rather than fixed.
	osg::ref_ptr<osg::Uniform> mainViewMatrix;
	osg::ref_ptr<osg::Uniform> mainViewMatrixInverse;

	bool valid() const;
};

// A fullscreen-quad lighting pass reading `gbuffer`. `mainCamera` is the real, on-screen viewer
// camera this pass rotates the G-buffer's view-space normal/position into world space with --
// the quad itself is necessarily ABSOLUTE_RF (an identity view/projection is what makes it cover
// the screen in NDC), so OSG's automatic osg_ViewMatrix resolves to identity here, not
// mainCamera's real matrices (the same PRE_RENDER-breaks-osg_ViewMatrix issue 11-sketchfab.py hit
// and fixed with a manually-maintained view-matrix uniform). **Call updatePBRIBLLightingPass()
// from a preDrawCallback on the FIRST PRE_RENDER camera in the scene graph (by render order),
// NOT from mainCamera's own preDrawCallback and NOT from application code after viewer.frame()
// returns** -- every PRE_RENDER camera finishes drawing before mainCamera's own preDrawCallback
// fires (confirmed against OSG 3.6.5's RenderStage::draw()), and a plain post-frame() call is a
// full frame later than that; either one hands this pass a stale matrix relative to what the
// geometry pass actually rendered with, which shows up as position/lighting artifacts that get
// worse while the camera is actively moving. This is exactly the bug class
// createPBRIBLGeometryPass()'s own `positionTexture` field exists to avoid for the PROJECTION
// matrix (which nested cameras can silently disagree about); the VIEW matrix genuinely is shared
// correctly across RELATIVE_RF-nested cameras, so this uniform only needs to be *fresh*, not
// reconstructed a different way.
PBRIBLLightingScene createPBRIBLLightingPass(
	const PBRIBLGBuffer& gbuffer,
	const PBRIBLEnvironment& environment,
	osg::Camera* mainCamera,
	float iblDiffuseIntensity=1.0f,
	float iblSpecularIntensity=1.0f,
	const PBRIBLLightingPassOptions& options={}
);

void updatePBRIBLLightingPass(PBRIBLLightingScene& scene, const osg::Camera* mainCamera);

}
