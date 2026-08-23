#pragma once

#include "Texture.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <map>
#include <string>

namespace osgDB { class Options; }
struct tg3_model;

namespace osgx::gltf::detail {

class MaterialBuilder {
public:
	MaterialBuilder(
		const tg3_model& model,
		const std::string& referrer,
		const osgDB::Options* readOptions,
		TextureLoader& textureLoader
	);

	void applyMaterial(
		int materialIndex,
		osg::Vec4& baseColorFactor,
		osg::Geometry* geometry,
		const std::map<int, osg::Array*>& textureCoordinateSets
	) const;

private:
	struct Environment {
		const std::string& _referrer;
		const osgDB::Options* _readOptions;
	};

	const tg3_model& _model;
	Environment _env;
	TextureLoader& _textureLoader;

	static float _srgbToLinear(float value);
	static float _solveMetallic(
		float diffuse,
		float specular,
		float oneMinusSpecularStrength
	);
	void _bakeSpecGlossToMetalRough(
		osg::Image* diffuseImage,
		const osg::Vec4& diffuseFactor,
		osg::Image* specularGlossinessImage,
		const osg::Vec3& specularFactor,
		float glossinessFactor,
		osg::ref_ptr<osg::Image>& outputBaseColor,
		osg::ref_ptr<osg::Image>& outputOrm
	) const;
	void _bakeOcclusionIntoOrm(
		osg::Image* occlusionImage,
		float strength,
		osg::Image* metallicRoughnessImage,
		osg::ref_ptr<osg::Image>& outputOrm
	) const;
};

}
