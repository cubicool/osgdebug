// Instantiate the single-header third-party implementations exactly once in osgx::gltf.
#include <osgx/Warnings.hpp>

OSGX_DISABLE_WARNINGS

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION

#include "tiny_gltf.h"

OSGX_ENABLE_WARNINGS
