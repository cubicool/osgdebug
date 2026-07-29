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
		{"F_MULTISCATTER", "osgx_F_MultiScatter", pbr::F_MULTISCATTER},
		{"IBL_SPECULAR", "osgx_IBLSpecular", pbr::IBL_SPECULAR},
		{"SPECULAR_AA", "osgx_SpecularAA", pbr::SPECULAR_AA},
		{"TONEMAP_PBR_NEUTRAL", "osgx_TonemapPBRNeutral", pbr::TONEMAP_PBR_NEUTRAL}
	};
	::osgx::registerShaderLibs("osgx::pbr", libs);
}

}
