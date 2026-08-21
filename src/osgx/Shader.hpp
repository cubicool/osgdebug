#pragma once

#include "Core.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Program>
#include <osg/Shader>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace osgx {

struct ShaderLib {
	std::string_view name;
	std::string_view glslName;
	std::string_view source;
};

// Registers a namespace's shader-library catalog (name/glslName/source triples) so
// resolveShaderLibs() can expand matching `#pragma <namespace> <lib1>[,<lib2>...]` directives
// found in GLSL source. Throws if namespaceName/libs is empty, or if a library with the same
// name is already registered under that namespace with different content.
void registerShaderLibs(std::string_view namespaceName, std::span<const ShaderLib> libs);

// Expands every `#pragma <namespace> <lib1>[,<lib2>...]` (or `#pragma <namespace> *` for all)
// directive in `src` whose namespace matches a registered catalog, inline-splicing the matching
// libraries' source. Pragmas for namespaces that were never registered are left untouched --
// this lets callers compose osgx's own snippet expansion with OSG's own state-driven shader
// variant pragmas (#pragma import_defines(...), import_modes(...), requires(...)) in the same
// source without conflict.
std::string resolveShaderLibs(std::string src);

// ================================================================================================
// Hook points: shader-object SUBSTITUTION, the counterpart to registerShaderLibs()/
// resolveShaderLibs()' text-splicing above. A Program-building call site (e.g.
// osgx::gltf::pbribl::PBRIBLScene::create()) declares which slots it supports via `defaults`;
// a caller overrides only the slots it cares about via `hooks`, leaving every other slot at that
// call site's own built-in. One shared enum/mechanism used everywhere osgx composes a Program
// this way, instead of each call site growing its own `osg::Shader* someHook=nullptr` parameter
// -- see TODO.md's HookList entry for the history (osgSlug's Atlas.hpp is the model).
// ================================================================================================

// Hook slots a Program-building call site MAY expose for shader-object substitution. Only add a
// new enumerator once a real call site wires it up -- see TODO.md's HookList entry for slots
// that are still discussed but not yet real parameters anywhere (e.g. direct/ambient lighting);
// don't pre-populate this for hypothetical future hooks.
enum class Hook {
	Tonemap,
	Skinning,
};

// One hook-slot override: which slot, and the shader object substituting the call site's own
// built-in definition for it.
using HookList = std::vector<std::pair<Hook, osg::ref_ptr<osg::Shader>>>;

// True if `hooks` contains an override for `hook`.
bool hasHook(const HookList& hooks, Hook hook);

// Attaches exactly one shader per slot in `defaults` to `program` -- the caller's override from
// `hooks` for that slot if present, otherwise `defaults`' own shader. `defaults` is the single
// source of truth for which slots this Program actually supports; every slot in it gets EXACTLY
// one definition attached, always -- never zero (a call with no definition is a link error only
// caught at OSG's realize-time GLObjectsVisitor precompile, not at Program-build time) and never
// two (GLSL permits one body per function, so a `hooks` entry SUBSTITUTES the default, it is
// never attached alongside it). Throws if `hooks` names a slot absent from `defaults` -- a hook
// this Program doesn't support, almost always a caller bug (typo, or a slot this call site
// doesn't actually have).
void applyHooks(osg::Program* program, const HookList& hooks, const HookList& defaults);

}
