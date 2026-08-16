#pragma once

#include "osgx/Warnings.hpp"

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

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "osgx/LambertianBake.hpp"

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
	// always exactly 3 (X/Y/Z row) vectors, one orthonormal basis. Bound to the shader as
	// a single `uniform vec3 iblAxis[3];` array; see osgx_OrientIBL() below.
	std::array<osg::Vec3, 3> iblAxis{
		osg::Vec3(0.0f, 0.0f, 1.0f),
		osg::Vec3(0.0f, 1.0f, 0.0f),
		osg::Vec3(-1.0f, 0.0f, 0.0f)
	};

	bool valid() const;
};

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

// Applies the glTF PBR/IBL renderer to a node using reusable prepared resources.
PBRIBLScene createPBRIBLScene(
	osg::Node* node,
	const PBRIBLEnvironment& environment,
	float iblDiffuseIntensity=1.0f,
	float iblSpecularIntensity=1.0f,
	bool diagnostics=false
);

}
