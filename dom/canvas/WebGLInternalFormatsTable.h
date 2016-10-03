// intentionally no include guard here.

#ifndef HANDLE_WEBGL_INTERNAL_FORMAT
#error This header is meant to be included by other files defining HANDLE_WEBGL_INTERNAL_FORMAT.
#endif

#define WEBGL_INTERNAL_FORMAT(effectiveinternalformat, unsizedinternalformat, type) \
  HANDLE_WEBGL_INTERNAL_FORMAT(LOCAL_GL_##effectiveinternalformat, \
                               LOCAL_GL_##unsizedinternalformat, \
                               LOCAL_GL_##type)

// OpenGL ES 3.0.3, Table 3.2
//
// Maps effective internal formats to (unsized internal format, type) pairs.
//
//                    Effective int. fmt.     Unsized int. fmt.   Type
WEBGL_INTERNAL_FORMAT(ALPHA8,                 ALPHA,              UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(LUMINANCE8,             LUMINANCE,          UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(LUMINANCE8_ALPHA8,      LUMINANCE_ALPHA,    UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RGB8,                   RGB,                UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RGBA4,                  RGBA,               UNSIGNED_SHORT_4_4_4_4)
WEBGL_INTERNAL_FORMAT(RGB5_A1,                RGBA,               UNSIGNED_SHORT_5_5_5_1)
WEBGL_INTERNAL_FORMAT(RGBA8,                  RGBA,               UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RGB10_A2,               RGBA,               UNSIGNED_INT_2_10_10_10_REV)
WEBGL_INTERNAL_FORMAT(DEPTH_COMPONENT16,      DEPTH_COMPONENT,    UNSIGNED_SHORT)
WEBGL_INTERNAL_FORMAT(DEPTH_COMPONENT24,      DEPTH_COMPONENT,    UNSIGNED_INT)
WEBGL_INTERNAL_FORMAT(R8,                     RED,                UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RG8,                    RG,                 UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(R16F,                   RED,                HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(R32F,                   RED,                FLOAT)
WEBGL_INTERNAL_FORMAT(RG16F,                  RG,                 HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(RG32F,                  RG,                 FLOAT)
WEBGL_INTERNAL_FORMAT(R8I,                    RED_INTEGER,        BYTE)
WEBGL_INTERNAL_FORMAT(R8UI,                   RED_INTEGER,        UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(R16I,                   RED_INTEGER,        SHORT)
WEBGL_INTERNAL_FORMAT(R16UI,                  RED_INTEGER,        UNSIGNED_SHORT)
WEBGL_INTERNAL_FORMAT(R32I,                   RED_INTEGER,        INT)
WEBGL_INTERNAL_FORMAT(R32UI,                  RED_INTEGER,        UNSIGNED_INT)
WEBGL_INTERNAL_FORMAT(RG8I,                   RG_INTEGER,         BYTE)
WEBGL_INTERNAL_FORMAT(RG8UI,                  RG_INTEGER,         UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RG16I,                  RG_INTEGER,         SHORT)
WEBGL_INTERNAL_FORMAT(RG16UI,                 RG_INTEGER,         UNSIGNED_SHORT)
WEBGL_INTERNAL_FORMAT(RG32I,                  RG_INTEGER,         INT)
WEBGL_INTERNAL_FORMAT(RG32UI,                 RG_INTEGER,         UNSIGNED_INT)
WEBGL_INTERNAL_FORMAT(RGBA32F,                RGBA,               FLOAT)
WEBGL_INTERNAL_FORMAT(RGB32F,                 RGB,                FLOAT)
WEBGL_INTERNAL_FORMAT(ALPHA32F_EXT,           ALPHA,              FLOAT)
WEBGL_INTERNAL_FORMAT(LUMINANCE32F_EXT,       LUMINANCE,          FLOAT)
WEBGL_INTERNAL_FORMAT(LUMINANCE_ALPHA32F_EXT, LUMINANCE_ALPHA,    FLOAT)
WEBGL_INTERNAL_FORMAT(RGBA16F,                RGBA,               HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(RGB16F,                 RGB,                HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(ALPHA16F_EXT,           ALPHA,              HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(LUMINANCE16F_EXT,       LUMINANCE,          HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(LUMINANCE_ALPHA16F_EXT, LUMINANCE_ALPHA,    HALF_FLOAT)
WEBGL_INTERNAL_FORMAT(DEPTH24_STENCIL8,       DEPTH_STENCIL,      UNSIGNED_INT_24_8)
WEBGL_INTERNAL_FORMAT(R11F_G11F_B10F,         RGB,                UNSIGNED_INT_10F_11F_11F_REV)
WEBGL_INTERNAL_FORMAT(RGB9_E5,                RGB,                UNSIGNED_INT_5_9_9_9_REV)
WEBGL_INTERNAL_FORMAT(SRGB8,                  SRGB,               UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(SRGB8_ALPHA8,           SRGB_ALPHA,         UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(DEPTH_COMPONENT32F,     DEPTH_COMPONENT,    FLOAT)
WEBGL_INTERNAL_FORMAT(DEPTH32F_STENCIL8,      DEPTH_STENCIL,      FLOAT_32_UNSIGNED_INT_24_8_REV)
WEBGL_INTERNAL_FORMAT(RGB565,                 RGB,                UNSIGNED_SHORT_5_6_5)
WEBGL_INTERNAL_FORMAT(RGBA32UI,               RGBA_INTEGER,       UNSIGNED_INT)
WEBGL_INTERNAL_FORMAT(RGB32UI,                RGB_INTEGER,        UNSIGNED_INT)
WEBGL_INTERNAL_FORMAT(RGBA16UI,               RGBA_INTEGER,       UNSIGNED_SHORT)
WEBGL_INTERNAL_FORMAT(RGB16UI,                RGB_INTEGER,        UNSIGNED_SHORT)
WEBGL_INTERNAL_FORMAT(RGBA8UI,                RGBA_INTEGER,       UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RGB8UI,                 RGB_INTEGER,        UNSIGNED_BYTE)
WEBGL_INTERNAL_FORMAT(RGBA32I,                RGBA_INTEGER,       INT)
WEBGL_INTERNAL_FORMAT(RGB32I,                 RGB_INTEGER,        INT)
WEBGL_INTERNAL_FORMAT(RGBA16I,                RGBA_INTEGER,       SHORT)
WEBGL_INTERNAL_FORMAT(RGB16I,                 RGB_INTEGER,        SHORT)
WEBGL_INTERNAL_FORMAT(RGBA8I,                 RGBA_INTEGER,       BYTE)
WEBGL_INTERNAL_FORMAT(RGB8I,                  RGB_INTEGER,        BYTE)
WEBGL_INTERNAL_FORMAT(R8_SNORM,               RED,                BYTE)
WEBGL_INTERNAL_FORMAT(RG8_SNORM,              RG,                 BYTE)
WEBGL_INTERNAL_FORMAT(RGB8_SNORM,             RGB,                BYTE)
WEBGL_INTERNAL_FORMAT(RGBA8_SNORM,            RGBA,               BYTE)
WEBGL_INTERNAL_FORMAT(RGB10_A2UI,             RGBA_INTEGER,       UNSIGNED_INT_2_10_10_10_REV)

#undef WEBGL_INTERNAL_FORMAT
#undef HANDLE_WEBGL_INTERNAL_FORMAT
