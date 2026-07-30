#include <osgx/gltf/Reader.hpp>

OSGX_DISABLE_WARNINGS

#include <osgDB/FileNameUtils>
#include <osgDB/Registry>

OSGX_ENABLE_WARNINGS

class ReaderWriterGLTF: public osgDB::ReaderWriter {
public:
	ReaderWriterGLTF() {
		supportsExtension("gltf", "glTF 2.0 ASCII");
		supportsExtension("glb", "glTF 2.0 binary");
	}

	const char* className() const override { return "glTF 2.0 plugin"; }

	ReadResult readObject(
		const std::string& location,
		const osgDB::Options* options
	) const override {
		return readNode(location, options);
	}

	ReadResult readNode(
		const std::string& location,
		const osgDB::Options* options
	) const override {
		const std::string ext = osgDB::getLowerCaseFileExtension(location);

		if(!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

		osgx::gltf::Reader reader;

		reader.setTextureCache(&_cache);

		return reader.read(location, ext == "glb", options);
	}

private:
	mutable osgx::gltf::Reader::TextureCache _cache;
};

REGISTER_OSGPLUGIN(gltf, ReaderWriterGLTF)
