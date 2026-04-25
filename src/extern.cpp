#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb/stb_image_write.h"

#include <assert.h>
#ifndef TEXR_ASSERT
#define TEXR_ASSERT(x) ((void)0)
#endif
#define TINYEXR_IMPLEMENTATION
#include "../external/ktx_software/external/astc-encoder/Source/ThirdParty/tinyexr.h"