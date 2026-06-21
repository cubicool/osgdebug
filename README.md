# osgDebug

osgDebug provides easy access to the various *_debug_* OpenGL extensions, allowing the code to provide additional/informative messages and annotations
that that applications like *Nsight* and *APITrace* support.

# `osgx.hpp` Extras

`osgx.hpp` is the small C++20 convenience layer that sits beside `osgDebug.hpp`; it keeps common OpenSceneGraph setup code shorter and adds a few modern range/span/lambda-friendly wrappers.

- `OSGX_DISABLE_WARNINGS` / `OSGX_ENABLE_WARNINGS` silence noisy OSG headers with compiler-specific diagnostic push/pop macros.
- `ObjectPath` is a simple dotted path accumulator used by scene-description visitors.
- `vec_t` and the `_v`, `_sz`, `_z` literals provide compact OSG scalar and size literals.
- `OSGReferenced`, `OSGArray`, and `OSGDrawElements` are concepts that constrain helpers to the expected OSG base types.
- `make_ref<T>()` and `make_nref<T>(name, ...)` create `osg::ref_ptr` objects, optionally naming the object immediately.
- `tick` and `call()` provide lightweight `osg::Timer`-based timing around arbitrary callables.
- `getFirstParent<T>()` walks an OSG parent chain and returns the first parent matching a requested type.
- `ring_buffer<T, N>` and `aring_buffer<T, N>` keep fixed-size recent samples, with the arithmetic version adding averages.
- `LambdaVisitor`, `IndexedVisitor`, `NameVisitor`, and `DescribeSceneVisitor` make common scene-graph traversal, naming, and tree-printing tasks less boilerplate-heavy.
- `VisitorEventHandler` runs a visitor against the viewer scene root when a key is released.
- `LambdaKeyHandler` binds one or more key presses to a lambda with optional access to the pressed key.
- `FilterNotifyHandler` filters known-noisy OSG notify messages with regexes before writing the rest to stderr.
- `Array<BaseArray>` wraps OSG array types with initializer/range constructors, append helpers, `std::span`, and range views; aliases include `Vec2Array`, `Vec3Array`, `Vec4Array`, and `FloatArray`.
- `DrawElements<BaseElements>` wraps indexed primitive sets with modern constructors, append helpers, primitive factories, bounds-checked pushes, spans, and views; aliases cover UByte, UShort, and UInt indices.
- `WriteTextureCallback` lets a camera draw callback write a texture to disk on demand.
- `CallbacksGroup` and its camera/node/drawable aliases fan out one OSG callback slot to multiple callbacks.
- `CameraDrawLambdaCallback` and `NodeLambdaCallback` adapt lambdas to common OSG callback types.
- The object-ID picking helpers create pick cameras, encode/decode 32-bit IDs, choose IDs from pick regions, and support sync or PBO-backed async readback.
- `PickCameraSync`, `PickHoverCallback`, and `PickHandler` wire pick cameras and readback state into viewer camera updates, hover transitions, and mouse input.
- `Ortho2DManipulator` is an orthographic 2D camera manipulator with pan, geometric zoom, pixel-nudge zoom, optional Ctrl-drag 3D tilt, and automatic near/far projection setup.

# Extensions

- [GL_KHR_debug](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt)
- GL_ARB_debug_output
- GL_EXT_debug_marker
- GL_EXT_debug_label
- GL_AMD_debug_output

# Examples

```
export LSAN_OPTIONS=suppressions=/home/cubicool/osgdebug/OSG/lsan.supp
export OSG_GL_CONTEXT_VERSION=3.0
```
