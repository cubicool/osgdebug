#include "osgx/PBR.hpp"

namespace osgx::pbr {

std::string snippets() {
	return std::string(D_GGX) + G_SCHLICK + G_SMITH + F_SCHLICK + F_SCHLICK_ROUGHNESS;
}

void OrbitLightRig::operator()(osg::Node* node, osg::NodeVisitor* nv) {
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

LightSet LightSet::create(osg::StateSet* ss) {
	LightSet lights;

	lights.ss = ss;

	ss->addUniform(new osg::Uniform("lightCount", 0));
	ss->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC4, "lightPosIntensity", MAX_LIGHTS));
	ss->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC3, "lightColor", MAX_LIGHTS));
	ss->addUniform(new osg::Uniform(osg::Uniform::INT, "lightType", MAX_LIGHTS));
	ss->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC3, "lightDir", MAX_LIGHTS));
	ss->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC2, "lightSpotAngles", MAX_LIGHTS));
	ss->addUniform(new osg::Uniform(osg::Uniform::FLOAT, "lightSourceRadius", MAX_LIGHTS));

	return lights;
}

void LightSet::setPoint(
	std::size_t index,
	const osg::Vec3& position,
	const osg::Vec3& color,
	float intensity,
	float sourceRadius
) const {
	auto i = static_cast<unsigned int>(index);

	ss->getUniform("lightPosIntensity")->setElement(i, osg::Vec4(position, intensity));
	ss->getUniform("lightColor")->setElement(i, color);
	ss->getUniform("lightType")->setElement(i, static_cast<int>(LightType::Point));
	ss->getUniform("lightSourceRadius")->setElement(i, sourceRadius);
}

void LightSet::setDirectional(
	std::size_t index,
	const osg::Vec3& direction,
	const osg::Vec3& color,
	float intensity
) const {
	auto i = static_cast<unsigned int>(index);

	ss->getUniform("lightDir")->setElement(i, direction);
	ss->getUniform("lightColor")->setElement(i, color);
	ss->getUniform("lightType")->setElement(i, static_cast<int>(LightType::Directional));
	ss->getUniform("lightPosIntensity")->setElement(i, osg::Vec4(0.0f, 0.0f, 0.0f, intensity));
	ss->getUniform("lightSourceRadius")->setElement(i, 0.0f);
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
	auto i = static_cast<unsigned int>(index);

	ss->getUniform("lightPosIntensity")->setElement(i, osg::Vec4(position, intensity));
	ss->getUniform("lightColor")->setElement(i, color);
	ss->getUniform("lightType")->setElement(i, static_cast<int>(LightType::Spot));
	ss->getUniform("lightDir")->setElement(i, direction);
	ss->getUniform("lightSpotAngles")->setElement(
		i, osg::Vec2(std::cos(innerConeAngle), std::cos(outerConeAngle))
	);
	ss->getUniform("lightSourceRadius")->setElement(i, sourceRadius);
}

void LightSet::setCount(int count) const {
	ss->getUniform("lightCount")->set(count);
}

}
