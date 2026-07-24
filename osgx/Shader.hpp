#pragma once

#include "Core.hpp"

namespace osgx {

struct ShaderLib {
	std::string_view name;
	std::string_view glslName;
	std::string_view source;
};

namespace detail {

struct ShaderLibCatalog {
	std::string namespaceName;
	std::vector<ShaderLib> libs;
};

inline std::vector<ShaderLibCatalog>& shaderLibCatalogs() {
	static std::vector<ShaderLibCatalog> catalogs;
	return catalogs;
}

inline bool shaderLibCatalogNameMatches(std::string_view lhs, std::string_view rhs) {
	return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](
		const char a, const char b
	) {
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	});
}

inline bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
	return text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), text.begin(), [](
		const char a, const char b
	) {
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	});
}

inline std::string_view trimShaderText(std::string_view text) {
	while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
	while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
	return text;
}

inline std::string normalizeShaderLibName(std::string_view name) {
	name = trimShaderText(name);
	if(name.starts_with("osgx_")) name.remove_prefix(5);
	std::string result;
	for(const char c : name) {
		if(std::isalnum(static_cast<unsigned char>(c))) result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return result;
}

inline bool shaderLibNameMatches(std::string_view requested, const ShaderLib& lib) {
	const auto normalized = normalizeShaderLibName(requested);
	return normalized == normalizeShaderLibName(lib.name) || normalized == normalizeShaderLibName(lib.glslName);
}

inline std::vector<std::string_view> splitShaderLibNames(std::string_view names) {
	if(const auto comment = names.find("//"); comment != std::string_view::npos) names = names.substr(0, comment);
	std::vector<std::string_view> result;
	for(size_t pos = 0; pos <= names.size();) {
		const auto comma = names.find(',', pos);
		const auto end = comma == std::string_view::npos ? names.size() : comma;
		if(const auto name = trimShaderText(names.substr(pos, end - pos)); !name.empty()) result.push_back(name);
		if(comma == std::string_view::npos) break;
		pos = comma + 1;
	}
	return result;
}

}

inline void registerShaderLibs(std::string_view namespaceName, std::span<const ShaderLib> libs) {
	if(namespaceName.empty() || libs.empty()) throw std::runtime_error("osgx::registerShaderLibs: namespace and catalog must not be empty");
	auto& catalogs = detail::shaderLibCatalogs();
	auto catalog = std::find_if(catalogs.begin(), catalogs.end(), [namespaceName](const auto& candidate) {
		return detail::shaderLibCatalogNameMatches(namespaceName, candidate.namespaceName);
	});
	if(catalog == catalogs.end()) catalog = catalogs.insert(catalogs.end(), {std::string(namespaceName), {}});
	for(const auto& lib : libs) {
		auto existing = std::find_if(catalog->libs.begin(), catalog->libs.end(), [&lib](const auto& candidate) {
			return detail::shaderLibCatalogNameMatches(lib.name, candidate.name);
		});
		if(existing == catalog->libs.end()) catalog->libs.push_back(lib);
		else if(existing->name != lib.name || existing->glslName != lib.glslName || existing->source != lib.source) {
			throw std::runtime_error("osgx::registerShaderLibs: conflicting library '" + std::string(lib.name) + "'");
		}
	}
}

inline std::string resolveShaderLibs(std::string src) {
	// Only pragmas whose namespace matches a registered osgx catalog are expanded.
	// Other pragmas are intentionally preserved. In particular, this lets callers
	// compose snippet expansion with OSG's state-driven shader variants, such as
	// #pragma import_defines(...), #pragma import_modes(...), and #pragma requires(...).
	static constexpr char CARRIAGE_RETURN = 13;
	static constexpr char LINE_FEED = 10;
	static constexpr char LINE_ENDINGS[] = {CARRIAGE_RETURN, LINE_FEED, 0};
	static constexpr char SHADER_WHITESPACE[] = {' ', 9, CARRIAGE_RETURN, LINE_FEED, 0};

	std::string resolved;
	resolved.reserve(src.size());
	for(size_t pos = 0; pos < src.size();) {
		const auto lineEnd = src.find_first_of(LINE_ENDINGS, pos);
		const auto lineSize = lineEnd == std::string::npos ? src.size() - pos : lineEnd - pos;
		const auto line = std::string_view(src.data() + pos, lineSize);
		auto rest = detail::trimShaderText(line);
		std::optional<std::string> replacement;
		if(detail::startsWithIgnoringCase(rest, "#pragma")) {
			rest = detail::trimShaderText(rest.substr(7));
			const auto nsEnd = rest.find_first_of(SHADER_WHITESPACE);
			const auto namespaceName = rest.substr(0, nsEnd);
			const auto names = nsEnd == std::string_view::npos ? std::string_view{} : rest.substr(nsEnd);
			for(const auto& catalog : detail::shaderLibCatalogs()) {
				if(!detail::shaderLibCatalogNameMatches(namespaceName, catalog.namespaceName)) continue;
				const auto requested = detail::splitShaderLibNames(names);
				if(requested.empty()) throw std::runtime_error("osgx::resolveShaderLibs: empty #pragma " + std::string(namespaceName));
				const bool all = requested.size() == 1 && requested.front() == "*";
				std::string expanded;
				for(const auto& lib : catalog.libs) if(all || std::any_of(requested.begin(), requested.end(), [&lib](auto name) { return detail::shaderLibNameMatches(name, lib); })) expanded += lib.source;
				if(!all) for(const auto name : requested) if(!std::any_of(catalog.libs.begin(), catalog.libs.end(), [name](const auto& lib) { return detail::shaderLibNameMatches(name, lib); })) throw std::runtime_error("osgx::resolveShaderLibs: unknown #pragma " + std::string(namespaceName) + " lib '" + std::string(name) + "'");
				replacement = std::move(expanded);
				break;
			}
		}
		if(replacement) resolved += *replacement; else resolved.append(line);
		if(lineEnd == std::string::npos) break;
		if(src[lineEnd] == CARRIAGE_RETURN && lineEnd + 1 < src.size() && src[lineEnd + 1] == LINE_FEED) {
			resolved += CARRIAGE_RETURN;
			resolved += LINE_FEED;
			pos = lineEnd + 2;
		}
		else { resolved += src[lineEnd]; pos = lineEnd + 1; }
	}
	return resolved;
}

}
