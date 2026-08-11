#include "osgx/ktx2/KTX2.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Image>
#include <osg/TextureCubeMap>

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgDB/Registry>

OSGX_ENABLE_WARNINGS

class ReaderWriterKTX2: public osgDB::ReaderWriter {
public:
	ReaderWriterKTX2() { supportsExtension("ktx2", "KTX 2.0 texture"); }

	const char* className() const override { return "KTX2 Texture Reader/Writer"; }

	ReadResult readObject(
		const std::string& file,
		const osgDB::Options* options
	) const override {
		if(!acceptsExtension(osgDB::getLowerCaseFileExtension(file))) {
			return ReadResult::FILE_NOT_HANDLED;
		}

		const std::string found = osgDB::findDataFile(file, options);

		if(found.empty()) return ReadResult::FILE_NOT_FOUND;

		return osgx::ktx2::read(found);
	}

	ReadResult readImage(
		const std::string& file,
		const osgDB::Options* options
	) const override {
		ReadResult result = readObject(file, options);

		if(!result.success()) return result;
		if(result.validImage()) return result;

		OSG_WARN
			<< "ReaderWriterKTX2: readImage() called on a cubemap KTX2 -- use readObject() instead"
			<< std::endl
		;

		return ReadResult::FILE_NOT_HANDLED;
	}

	WriteResult writeObject(
		const osg::Object& object,
		const std::string& file,
		const osgDB::Options*
	) const override {
		if(!acceptsExtension(osgDB::getLowerCaseFileExtension(file))) {
			return WriteResult::FILE_NOT_HANDLED;
		}

		if(const auto* texture = dynamic_cast<const osg::TextureCubeMap*>(&object)) {
			return osgx::ktx2::write(*texture, file);
		}

		if(const auto* image = dynamic_cast<const osg::Image*>(&object)) {
			return osgx::ktx2::write(*image, file);
		}

		return WriteResult::FILE_NOT_HANDLED;
	}

	WriteResult writeImage(
		const osg::Image& image,
		const std::string& file,
		const osgDB::Options*
	) const override {
		if(!acceptsExtension(osgDB::getLowerCaseFileExtension(file))) {
			return WriteResult::FILE_NOT_HANDLED;
		}

		return osgx::ktx2::write(image, file);
	}
};

// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(ktx2, ReaderWriterKTX2)
