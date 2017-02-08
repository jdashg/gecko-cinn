/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLBLITHELPER_H_
#define GLBLITHELPER_H_

#include "GLContextTypes.h"
#include "GLConsts.h"
#include "nsSize.h"
#include "mozilla/Attributes.h"
#include "mozilla/gfx/Point.h"

namespace mozilla {

namespace layers {
class Image;
class PlanarYCbCrImage;
class SurfaceTextureImage;
class MacIOSurfaceImage;
class EGLImageImage;
} // namespace layers

namespace gl {

class GLContext;

class GLBlitHelper final
{
    friend class GLContext;

    enum class Channel {
        Y = 0,
        Cb,
        Cr,

        SIZE
    };

    /**
     * BlitTex2D is used to copy blit the content of a GL_TEXTURE_2D object,
     * BlitTexRect is used to copy blit the content of a GL_TEXTURE_RECT object,
     * The difference between BlitTex2D and BlitTexRect is the texture type, which affect
     * the fragment shader a bit.
     *
     * ConvertPlnarYcbCr is used to color convert copy blit the PlanarYCbCrImage
     * into a normal RGB texture by create textures of each color channel, and
     * convert it in GPU.
     * Convert type is created for canvas.
     */
    enum class BlitType {
        BlitTex2D = 0,
        BlitTexRect,
        ConvertPlanarYCbCr,
        ConvertSurfaceTexture,
        ConvertEGLImage,
        ConvertMacIOSurfaceImage,

        SIZE
    };

    // The GLContext is the sole owner of the GLBlitHelper.
    GLContext* const mGL;

    GLuint mTexBlit_Buffer;
    GLuint mTexBlit_Programs[size_t(BlitType::SIZE)];
    GLint mTexBlit_uYFlip[size_t(BlitType::SIZE)];

    GLint mBlitTexRect_uTexCoordMult;

    GLint mConvertSurfaceTexture_uTextureTransform;

    GLint mConvertPlanarYCbCr_uYTexScale;
    GLint mConvertPlanarYCbCr_uCbCrTexScale;
    GLint mConvertPlanarYCbCr_uYuvColorMatrix;

    GLint mConvertMacIOSurfaceImage_uYTexScale;
    GLint mConvertMacIOSurfaceImage_uCbCrTexScale;

    // Data for image blit path
    GLuint mYUVTextures[size_t(Channel::SIZE)];
    int32_t mYUVTexture_Y_Width;
    int32_t mYUVTexture_Y_Height;

    GLuint mFBO;
    GLuint mSrcTexEGL;

    ////////////////

    void SetBlitFramebufferForDestTexture(GLuint aTexture);

    bool UseTexQuadProgram(BlitType type);
    void InitTexQuadPrograms();
    void BindAndUploadYUVTexture(Channel which, uint32_t width, uint32_t height, void* data, bool allocation);
    void BindAndUploadEGLImage(EGLImage image, GLuint target);

    bool BlitPlanarYCbCrImage(layers::PlanarYCbCrImage* yuvImage);
#ifdef MOZ_WIDGET_ANDROID
    // Blit onto the current FB.
    bool BlitSurfaceTextureImage(layers::SurfaceTextureImage* stImage);
    bool BlitEGLImageImage(layers::EGLImageImage* eglImage);
#endif
#ifdef XP_MACOSX
    bool BlitMacIOSurfaceImage(layers::MacIOSurfaceImage* ioImage);
#endif

    explicit GLBlitHelper(GLContext* gl);

public:
    ~GLBlitHelper();

    // If you don't have |srcFormats| for the 2nd definition,
    // then you'll need the framebuffer_blit extensions to use
    // the first BlitFramebufferToFramebuffer.
    void BlitFramebufferToFramebuffer(GLuint srcFB, GLuint destFB,
                                      const gfx::IntSize& srcSize,
                                      const gfx::IntSize& destSize,
                                      bool internalFBs = false) const;
    void BlitFramebufferToFramebuffer(GLuint srcFB, GLuint destFB,
                                      const gfx::IntSize& srcSize,
                                      const gfx::IntSize& destSize,
                                      const GLFormats& srcFormats,
                                      bool internalFBs = false);
    void BlitTextureToFramebuffer(GLuint srcTex, GLuint destFB,
                                  const gfx::IntSize& srcSize,
                                  const gfx::IntSize& destSize,
                                  GLenum srcTarget = LOCAL_GL_TEXTURE_2D,
                                  bool internalFBs = false);
    void DrawBlitTextureToFramebuffer(GLuint srcTex, GLuint destFB,
                                      const gfx::IntSize& srcSize,
                                      const gfx::IntSize& destSize,
                                      GLenum srcTarget = LOCAL_GL_TEXTURE_2D,
                                      bool internalFBs = false);
    void BlitFramebufferToTexture(GLuint srcFB, GLuint destTex,
                                  const gfx::IntSize& srcSize,
                                  const gfx::IntSize& destSize,
                                  GLenum destTarget = LOCAL_GL_TEXTURE_2D,
                                  bool internalFBs = false) const;
    void BlitTextureToTexture(GLuint srcTex, GLuint destTex,
                              const gfx::IntSize& srcSize,
                              const gfx::IntSize& destSize,
                              GLenum srcTarget = LOCAL_GL_TEXTURE_2D,
                              GLenum destTarget = LOCAL_GL_TEXTURE_2D) const;
    bool BlitImageToFramebuffer(layers::Image* srcImage, const gfx::IntSize& destSize,
                                GLuint destFB, OriginPos destOrigin);
    bool BlitImageToTexture(layers::Image* srcImage, const gfx::IntSize& destSize,
                            GLuint destTex, GLenum destTarget, OriginPos destOrigin);
};

} // namespace gl
} // namespace mozilla

#endif // GLBLITHELPER_H_
