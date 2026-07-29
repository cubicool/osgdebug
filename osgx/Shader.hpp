#pragma once

#include <span>
#include <string>
#include <string_view>

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

}
