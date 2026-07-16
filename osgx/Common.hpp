#pragma once

#if defined(__clang__)
	#define OSGX_DISABLE_WARNINGS \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Wconversion\"") \
		_Pragma("clang diagnostic ignored \"-Wdeprecated-copy\"") \
		_Pragma("clang diagnostic ignored \"-Wfloat-conversion\"") \
		_Pragma("clang diagnostic ignored \"-Wsign-compare\"") \
		_Pragma("clang diagnostic ignored \"-Woverloaded-virtual\"") \
		_Pragma("clang diagnostic ignored \"-Wshadow\"") \
		_Pragma("clang diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("clang diagnostic ignored \"-Wextra\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("clang diagnostic pop")

#elif defined(__GNUC__)
	#define OSGX_DISABLE_WARNINGS \
		_Pragma("GCC diagnostic push") \
		_Pragma("GCC diagnostic ignored \"-Wconversion\"") \
		_Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"") \
		_Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
		_Pragma("GCC diagnostic ignored \"-Wsign-compare\"") \
		_Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"") \
		_Pragma("GCC diagnostic ignored \"-Wshadow\"") \
		_Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("GCC diagnostic ignored \"-Wextra\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("GCC diagnostic pop")

#else
	#define OSGX_DISABLE_WARNINGS
	#define OSGX_ENABLE_WARNINGS
#endif

// The same as `META_Object` but updated for modern C++ warnings.
#define OSGX_META_Object(library,name) \
	osg::Object* cloneType() const override { return new name (); } \
	osg::Object* clone(const osg::CopyOp& copyop) const override { return new name (*this,copyop); } \
	bool isSameKindAs(const osg::Object* obj) const override { return dynamic_cast<const name *>(obj)!=NULL; } \
	const char* libraryName() const override { return #library; }\
	const char* className() const override { return #name; }

OSGX_DISABLE_WARNINGS

// TODO: Trim these down to only the essential when this header settles.
#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/ShapeDrawable>
#include <osg/PrimitiveSet>

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/GLExtensions>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgGA/CameraManipulator>

#include <osgViewer/View>
// #include <osgViewer/ViewerBase>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <numeric>
#include <regex>
#include <concepts>
#include <ranges>
#include <span>
#include <atomic>
#include <stdexcept>
#include <unordered_map>

