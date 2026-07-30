#pragma once

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Camera>
#include <osg/Group>
#include <osg/Node>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/Uniform>
#include <osg/Vec3>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <string>
#include <string_view>
#include <vector>

#include <osgx/LambertianBake.hpp>

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

// Registers the osgGLTF shader catalog used by `#pragma osgGLTF ...`. Registration is idempotent.
void registerShaderLibs();

// Registers the generic osgx PBR/IBL catalogs plus osgGLTF's catalog, then expands them together.
// Keeping registration and resolution in this component avoids cross-shared-library registry
// assumptions for Python and plugin consumers.
std::string resolveShaderLibs(std::string_view source);

// Prepared IBL resources. `root`, when present, contains the PRE_RENDER passes that populate the
// generated BRDF LUT and diffuse cubemap; add it to a rendered scene graph before using them.
// Pre-baked resources have no preparation root and can leave it null.
struct PBRIBLEnvironment {
	osg::ref_ptr<osg::Group> root;
	osg::ref_ptr<osg::Camera> lutCamera;
	osg::ref_ptr<osg::Group> diffuseBakeRoot;
	// Only present when the specular cubemap came from preparePBRIBLEnvironment(hdrPath, ...) --
	// the pre-baked-KTX2 overload has no bake to drive and leaves this null.
	osg::ref_ptr<osg::Group> specularBakeRoot;
	osg::ref_ptr<osg::TextureCubeMap> envMap;
	osg::ref_ptr<osg::Texture2D> brdfLUT;
	osg::ref_ptr<osg::TextureCubeMap> diffuseEnv;
	// KTX/OpenGL cubemap lookup basis, expressed relative to osgGLTF's Z-up world.
	osg::Vec3 iblAxisX{0.0f, 0.0f, 1.0f};
	osg::Vec3 iblAxisY{0.0f, 1.0f, 0.0f};
	osg::Vec3 iblAxisZ{-1.0f, 0.0f, 0.0f};

	bool valid() const;
};

// Pre-baked specular path: loads a finished GGX-prefiltered KTX2 from disk, still bakes diffuse
// irradiance and the BRDF LUT live from `hdrPath`. Kept for the Khronos-parity harness and for
// callers with an existing offline `osggltf-iblbake-gpu` bake to reuse.
PBRIBLEnvironment preparePBRIBLEnvironment(const std::string& ktx2Path, const std::string& hdrPath, int lutSize=1024);

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
	// discussion in osgdebug/TODO.md for why diffuse/brdfLUT don't need equivalents yet.
	struct SpecularResource: Resource {
		int prefilterSize = 0;
		int lowestMipLevel = 0;
	};

	SpecularResource specular;
	Resource diffuse;
	Resource brdfLUT;
};

// Decodes every `environments[]` entry out of an `osgx_pbribl` extension block. `extensionValue`
// is whatever `tinygltf::Model::extensions.at("osgx_pbribl")` returns -- tinygltf parses the root
// `extensions` block through the same generic path whether it came from a minimal standalone
// manifest document or a real asset's own embedded extension, so this one function covers both.
std::vector<IBLEnvironmentManifest> decodeIBLEnvironments(const tinygltf::Value& extensionValue);

// Fully static/pre-baked path: loads all three IBL resources from disk with NO HDR decode/bake at
// runtime at all -- specular + diffuse cubemaps as KTX2, BRDF LUT as a plain image wrapped into a
// Texture2D (see osggltf-iblbake-gpu's --brdf-lut-only mode for why the LUT was never an HDR-
// derived output). This is the TODO.md 2b "--env <environment.json>" shipping path. The returned
// PBRIBLEnvironment has no bake root at all -- there is nothing left to bake, matching the struct's
// own "pre-baked resources... can leave it null" contract. `manifest`'s relative URIs resolve
// against `baseDir` (normally the manifest document's own directory).
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

	bool valid() const;
};

// Applies osgGLTF's renderer to a node using reusable prepared resources.
PBRIBLScene createPBRIBLScene(
	osg::Node* node,
	const PBRIBLEnvironment& environment,
	float iblIntensity=1.0f,
	bool diagnostics=false
);

}
