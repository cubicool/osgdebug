#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Notify>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <ostream>

// indent doubles as a verbosity level: 0-1 are the coarse, top-level lines (stage summaries,
// "loading X"), 2+ is the deep per-vertex/per-joint detail. Mapping it straight to a
// osg::NotifySeverity means verbosity is controlled through the normal, already-runtime-settable
// OSG mechanism (osg::setNotifyLevel()/OSG_NOTIFY_LEVEL) instead of a bespoke knob.
inline osg::NotifySeverity gltfNotifySeverity(std::size_t indent) {
	return indent <= 1 ? osg::INFO : osg::DEBUG_INFO;
}

inline std::ostream& gltfNotify(std::size_t indent) {
	std::ostream& out = osg::notify(gltfNotifySeverity(indent)) << "[GLTF] ";

	for(std::size_t i = 0; i < indent; i++) out << "  ";

	return out;
}

#define GLTF_NOTIFY(indent) \
	if(osg::isNotifyEnabled(gltfNotifySeverity(indent))) gltfNotify(indent)
