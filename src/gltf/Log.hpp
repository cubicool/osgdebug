#pragma once

#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#include <osg/Notify>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <ostream>

// Change this to osg::INFO/osg::DEBUG_INFO/etc. to reduce verbosity.
#define GLTF_NOTIFY_SEVERITY osg::NOTICE

inline std::ostream& gltfNotify(osg::NotifySeverity severity, std::size_t indent = 0) {
	std::ostream& out = osg::notify(severity) << "[GLTF] ";

	for(std::size_t i = 0; i < indent; i++) out << "  ";

	return out;
}

#define GLTF_NOTIFY(indent) \
	if(osg::isNotifyEnabled(GLTF_NOTIFY_SEVERITY)) gltfNotify(GLTF_NOTIFY_SEVERITY, indent)
