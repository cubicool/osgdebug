# osgDebug

osgDebug provides easy access to the various *_debug_* OpenGL extensions, allowing the code to provide additional/informative messages and annotations
that that applications like *Nsight* and *APITrace* support.

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
