#include "osgx/PBR.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace osgx::pbr {

std::string snippets() {
	return std::string(D_GGX) + G_SCHLICK + G_SMITH + F_SCHLICK + F_SCHLICK_ROUGHNESS;
}

void OrbitLightRig::operator()(osg::Node* node, osg::NodeVisitor* nv) {
	float t = nv->getFrameStamp() ? float(nv->getFrameStamp()->getSimulationTime()) : 0.0f;

	if(orbits.size() > static_cast<std::size_t>(MAX_LIGHTS)) {
		throw std::out_of_range("OrbitLightRig has more orbits than LightSet supports");
	}

	for(std::size_t i = 0; i < orbits.size(); i++) {
		const auto& o = orbits[i];
		float a = t * o.speed + o.phase;

		lights.setPosition(
			i,
			osg::Vec3(
				center.x() + std::cos(a) * o.radius,
				center.y() + std::sin(a) * o.radius,
				center.z() + o.height
			),
			o.intensity * intensity
		);
	}

	traverse(node, nv);
}

namespace detail {

// Float offsets into one packed osgx_Light struct (LIGHT_STRUCT_FLOATS=16 floats/64 bytes) --
// must match LIGHT_UNIFORMS' GLSL layout comment in PBR.hpp exactly.
constexpr std::size_t POS_INTENSITY_OFFSET = 0; // vec4
constexpr std::size_t COLOR_OFFSET = 4; // vec3
constexpr std::size_t TYPE_OFFSET = 7; // int
constexpr std::size_t DIR_OFFSET = 8; // vec3
constexpr std::size_t SOURCE_RADIUS_OFFSET = 11; // float
constexpr std::size_t SPOT_ANGLES_OFFSET = 12; // vec2

// The `type` field is declared `int` on the GLSL side (LIGHT_UNIFORMS) but stored in this
// float-typed backing array -- std::bit_cast reinterprets the bit pattern without UB (unlike a
// union-based type pun), which is all that's needed since the GPU reads the same raw bytes back
// as int regardless of how the CPU side labeled the storage.
float intBitsToFloat(int value) { return std::bit_cast<float>(value); }
int floatBitsToInt(float value) { return std::bit_cast<int>(value); }

}

LightSet LightSet::create(osg::StateSet* ss) {
	if(!ss) throw std::invalid_argument("LightSet::create requires a StateSet");

	LightSet result;

	result.ss = ss;
	result._lights = new osg::FloatArray(static_cast<unsigned int>(MAX_LIGHTS * LIGHT_STRUCT_FLOATS));

	std::fill(result._lights->begin(), result._lights->end(), 0.0f);
	result._lights->setBufferObject(new osg::ShaderStorageBufferObject());

	ss->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(
			LIGHT_SSBO_BINDING,
			result._lights.get(),
			0,
			static_cast<GLsizeiptr>(result._lights->getTotalDataSize())
		),
		osg::StateAttribute::ON
	);
	ss->addUniform(new osg::Uniform("osgx_lightCount", 0));

	return result;
}

bool LightSet::valid() const {
	return ss.valid() && _lights.valid() && ss->getUniform("osgx_lightCount");
}

float* LightSet::lightFloats(std::size_t index, std::size_t offset) const {
	if(!valid()) throw std::logic_error("LightSet has not been created");
	if(index >= static_cast<std::size_t>(MAX_LIGHTS)) throw std::out_of_range("LightSet index out of range");

	return &(*_lights)[index * LIGHT_STRUCT_FLOATS + offset];
}

void LightSet::setPoint(
	std::size_t index,
	const osg::Vec3& position,
	const osg::Vec3& color,
	float intensity,
	float sourceRadius
) const {
	auto* posIntensity = lightFloats(index, detail::POS_INTENSITY_OFFSET);
	auto* colorFloats = lightFloats(index, detail::COLOR_OFFSET);
	auto* typeFloats = lightFloats(index, detail::TYPE_OFFSET);
	auto* radiusFloats = lightFloats(index, detail::SOURCE_RADIUS_OFFSET);

	posIntensity[0] = position.x();
	posIntensity[1] = position.y();
	posIntensity[2] = position.z();
	posIntensity[3] = intensity;
	colorFloats[0] = color.x();
	colorFloats[1] = color.y();
	colorFloats[2] = color.z();
	typeFloats[0] = detail::intBitsToFloat(static_cast<int>(LightType::Point));
	radiusFloats[0] = sourceRadius;

	_lights->dirty();
}

void LightSet::setDirectional(
	std::size_t index,
	const osg::Vec3& direction,
	const osg::Vec3& color,
	float intensity
) const {
	auto* posIntensity = lightFloats(index, detail::POS_INTENSITY_OFFSET);
	auto* colorFloats = lightFloats(index, detail::COLOR_OFFSET);
	auto* typeFloats = lightFloats(index, detail::TYPE_OFFSET);
	auto* dirFloats = lightFloats(index, detail::DIR_OFFSET);
	auto* radiusFloats = lightFloats(index, detail::SOURCE_RADIUS_OFFSET);

	posIntensity[0] = 0.0f;
	posIntensity[1] = 0.0f;
	posIntensity[2] = 0.0f;
	posIntensity[3] = intensity;
	colorFloats[0] = color.x();
	colorFloats[1] = color.y();
	colorFloats[2] = color.z();
	typeFloats[0] = detail::intBitsToFloat(static_cast<int>(LightType::Directional));
	dirFloats[0] = direction.x();
	dirFloats[1] = direction.y();
	dirFloats[2] = direction.z();
	radiusFloats[0] = 0.0f;

	_lights->dirty();
}

void LightSet::setSpot(
	std::size_t index,
	const osg::Vec3& position,
	const osg::Vec3& direction,
	const osg::Vec3& color,
	float intensity,
	float innerConeAngle,
	float outerConeAngle,
	float sourceRadius
) const {
	auto* posIntensity = lightFloats(index, detail::POS_INTENSITY_OFFSET);
	auto* colorFloats = lightFloats(index, detail::COLOR_OFFSET);
	auto* typeFloats = lightFloats(index, detail::TYPE_OFFSET);
	auto* dirFloats = lightFloats(index, detail::DIR_OFFSET);
	auto* radiusFloats = lightFloats(index, detail::SOURCE_RADIUS_OFFSET);
	auto* spotFloats = lightFloats(index, detail::SPOT_ANGLES_OFFSET);

	posIntensity[0] = position.x();
	posIntensity[1] = position.y();
	posIntensity[2] = position.z();
	posIntensity[3] = intensity;
	colorFloats[0] = color.x();
	colorFloats[1] = color.y();
	colorFloats[2] = color.z();
	typeFloats[0] = detail::intBitsToFloat(static_cast<int>(LightType::Spot));
	dirFloats[0] = direction.x();
	dirFloats[1] = direction.y();
	dirFloats[2] = direction.z();
	radiusFloats[0] = sourceRadius;
	spotFloats[0] = std::cos(innerConeAngle);
	spotFloats[1] = std::cos(outerConeAngle);

	_lights->dirty();
}

void LightSet::setCount(int count) const {
	if(!valid()) throw std::logic_error("LightSet has not been created");
	if(count < 0 || count > MAX_LIGHTS) throw std::out_of_range("LightSet count out of range");

	ss->getUniform("osgx_lightCount")->set(count);
}

void LightSet::setPosition(std::size_t index, const osg::Vec3& position, float intensity) const {
	auto* posIntensity = lightFloats(index, detail::POS_INTENSITY_OFFSET);

	posIntensity[0] = position.x();
	posIntensity[1] = position.y();
	posIntensity[2] = position.z();
	posIntensity[3] = intensity;

	_lights->dirty();
}

int LightSet::getCount() const {
	if(!valid()) throw std::logic_error("LightSet has not been created");

	int count = 0;

	ss->getUniform("osgx_lightCount")->get(count);

	return count;
}

osg::Vec4 LightSet::getPosIntensity(std::size_t index) const {
	auto* f = lightFloats(index, detail::POS_INTENSITY_OFFSET);

	return osg::Vec4(f[0], f[1], f[2], f[3]);
}

osg::Vec3 LightSet::getColor(std::size_t index) const {
	auto* f = lightFloats(index, detail::COLOR_OFFSET);

	return osg::Vec3(f[0], f[1], f[2]);
}

LightType LightSet::getType(std::size_t index) const {
	auto* f = lightFloats(index, detail::TYPE_OFFSET);

	return static_cast<LightType>(detail::floatBitsToInt(f[0]));
}

osg::Vec3 LightSet::getDirection(std::size_t index) const {
	auto* f = lightFloats(index, detail::DIR_OFFSET);

	return osg::Vec3(f[0], f[1], f[2]);
}

osg::Vec2 LightSet::getSpotAngles(std::size_t index) const {
	auto* f = lightFloats(index, detail::SPOT_ANGLES_OFFSET);

	return osg::Vec2(f[0], f[1]);
}

float LightSet::getSourceRadius(std::size_t index) const {
	auto* f = lightFloats(index, detail::SOURCE_RADIUS_OFFSET);

	return f[0];
}

void registerShaderLibs() {
	static constexpr ShaderLib libs[] = {
		{"D_GGX", "osgx_D_GGX", pbr::D_GGX},
		{"G_SCHLICK", "osgx_G_Schlick", pbr::G_SCHLICK},
		{"G_SMITH", "osgx_G_Smith", pbr::G_SMITH},
		{"F_SCHLICK", "osgx_F_Schlick", pbr::F_SCHLICK},
		{"F_SCHLICK_ROUGHNESS", "osgx_F_Schlick_roughness", pbr::F_SCHLICK_ROUGHNESS},
		{"MATERIAL_STRUCT", "osgx_Material", pbr::MATERIAL_STRUCT},
		{"DIRECT_SPECULAR", "osgx_DirectSpecular", pbr::DIRECT_SPECULAR},
		{"DIRECT_DIFFUSE", "osgx_DirectDiffuse", pbr::DIRECT_DIFFUSE},
		{"POINT_LIGHT_RADIANCE", "osgx_PointLightRadiance", pbr::POINT_LIGHT_RADIANCE},
		{"LIGHT_UNIFORMS", "osgx_LightUniforms", pbr::LIGHT_UNIFORMS},
		{"DIRECT_LIGHT", "osgx_DirectLight", pbr::DIRECT_LIGHT},
		{"DIRECTIONAL_LIGHT_RADIANCE", "osgx_DirectionalLightRadiance", pbr::DIRECTIONAL_LIGHT_RADIANCE},
		{"SPOT_LIGHT_RADIANCE", "osgx_SpotLightRadiance", pbr::SPOT_LIGHT_RADIANCE},
		{"SPHERE_LIGHT_SPECULAR", "osgx_SphereLightDir", pbr::SPHERE_LIGHT_SPECULAR},
		{"DIRECT_LIGHT_SPHERE", "osgx_DirectLightSphere", pbr::DIRECT_LIGHT_SPHERE},
		{"LIGHT_SHADE_DECL", "osgx_ShadeDirect", pbr::LIGHT_SHADE_DECL},
		{"F_MULTISCATTER", "osgx_F_MultiScatter", pbr::F_MULTISCATTER},
		{"IBL_SPECULAR", "osgx_IBLSpecular", pbr::IBL_SPECULAR},
		{"SPECULAR_AA", "osgx_SpecularAA", pbr::SPECULAR_AA},
		{"TONEMAP_PBR_NEUTRAL", "osgx_TonemapPBRNeutral", pbr::TONEMAP_PBR_NEUTRAL}
	};
	::osgx::registerShaderLibs("osgx::pbr", libs);
}

}
