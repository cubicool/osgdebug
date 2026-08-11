//vimrun! OSG_NOTIFY_LEVEL=NOTICE osgviewerd ~/OpenSceneGraph-Data/cow.osgt.debug

#include "osgx/Debug.hpp"
#include "osgx/Visitors.hpp"

OSGX_DISABLE_WARNINGS

#include <osgDB/ReaderWriter>
#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <osgDB/ReadFile>

OSGX_ENABLE_WARNINGS

#define EXTENSION_NAME "debug"

class ReaderWriterDEBUG: public osgDB::ReaderWriter {
public:
	ReaderWriterDEBUG() {
		supportsExtension(EXTENSION_NAME, "osgDebug Pseudo-loader");
	}

	virtual const char* className() const { return "osgDebug Pseudo-loader"; }

	virtual bool acceptsExtension(const std::string& extension) const {
		return osgDB::equalCaseInsensitive(extension, EXTENSION_NAME);
	}

	virtual ReadResult readObject(
		const std::string& fileName,
		const osgDB::ReaderWriter::Options* options
	) const {
		return readNode(fileName, options);
	}

	virtual ReadResult readNode(
		const std::string& fileName,
		const osgDB::ReaderWriter::Options* options
	) const {
		auto ext = osgDB::getLowerCaseFileExtension(fileName);

		if(!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

		OSG_NOTICE << "ReaderWriterDEBUG(\"" << fileName << "\")" << std::endl;

		auto name = osgDB::getNameLessExtension(fileName);

		if(name.empty()) return ReadResult::FILE_NOT_HANDLED;

		auto node = osgDB::readRefNodeFile(name, options);

		if(!node) {
			OSG_WARN << "File \"" << name << "\" could not be loaded" << std::endl;

			return ReadResult::FILE_NOT_HANDLED;
		}

		auto dsv = osgx::DescribeSceneVisitor();
		auto dv = osgx::debug::DrawVisitor();

		node->accept(dsv);
		node->accept(dv);

		return node;
	}
};

// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(debug, ReaderWriterDEBUG)
