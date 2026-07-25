#pragma once

#if defined(__clang__)
	#define OSGX_DISABLE_WARNINGS \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Wconversion\"") \
		_Pragma("clang diagnostic ignored \"-Wdeprecated-copy\"") \
		_Pragma("clang diagnostic ignored \"-Wfloat-conversion\"") \
		_Pragma("clang diagnostic ignored \"-Wsign-conversion\"") \
		_Pragma("clang diagnostic ignored \"-Wsign-compare\"") \
		_Pragma("clang diagnostic ignored \"-Woverloaded-virtual\"") \
		_Pragma("clang diagnostic ignored \"-Wshadow\"") \
		_Pragma("clang diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("clang diagnostic ignored \"-Wunused-function\"") \
		_Pragma("clang diagnostic ignored \"-Wextra\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("clang diagnostic pop")

#elif defined(__GNUC__)
	#define OSGX_DISABLE_WARNINGS \
		_Pragma("GCC diagnostic push") \
		_Pragma("GCC diagnostic ignored \"-Wconversion\"") \
		_Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"") \
		_Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
		_Pragma("GCC diagnostic ignored \"-Wsign-conversion\"") \
		_Pragma("GCC diagnostic ignored \"-Wsign-compare\"") \
		_Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"") \
		_Pragma("GCC diagnostic ignored \"-Wshadow\"") \
		_Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
		_Pragma("GCC diagnostic ignored \"-Wunused-function\"") \
		_Pragma("GCC diagnostic ignored \"-Wextra\"")

	#define OSGX_ENABLE_WARNINGS \
		_Pragma("GCC diagnostic pop")

#else
	#define OSGX_DISABLE_WARNINGS
	#define OSGX_ENABLE_WARNINGS
#endif
