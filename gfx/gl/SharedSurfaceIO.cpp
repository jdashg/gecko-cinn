/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceIO.h"

#include "GLContextCGL.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/gfx/MacIOSurface.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace gl {

SharedSurface_IOSurface::SharedSurface_IOSurface(GLContext* const gl,
                                                 const gfx::IntSize& size,
                                                 UniquePtr<MozFramebuffer>&& mozFB,
                                                 MacIOSurface* const ioSurf)
    : SharedSurface(SharedSurfaceType::IOSurface, gl, size, true, mozFB)
    , mIOSurf(ioSurf)
{ }

void
SharedSurface_IOSurface::ProducerReleaseImpl()
{
    mGL->MakeCurrent();
    mGL->fFlush();
}

bool
SharedSurface_IOSurface::CopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                        GLint x, GLint y, GLsizei width, GLsizei height,
                                        GLint border)
{
    /* Bug 896693 - OpenGL framebuffers that are backed by IOSurface on OSX expose a bug
     * in glCopyTexImage2D --- internalformats GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA
     * return the wrong results. To work around, copy framebuffer to a temporary texture
     * using GL_RGBA (which works), attach as read framebuffer and glCopyTexImage2D
     * instead.
     */

    // https://www.opengl.org/sdk/docs/man3/xhtml/glCopyTexImage2D.xml says that width or
    // height set to 0 results in a NULL texture. Lets not do any work and punt to
    // original glCopyTexImage2D, since the FBO below will fail when trying to attach a
    // texture of 0 width or height.
    if (width == 0 || height == 0)
        return false;

    switch (internalformat) {
    case LOCAL_GL_ALPHA:
    case LOCAL_GL_LUMINANCE:
    case LOCAL_GL_LUMINANCE_ALPHA:
        break;

    default:
        return false;
    }

    MOZ_ASSERT(mGL->IsCurrent());

    ScopedTexture destTex(mGL);
    {
        ScopedBindTexture bindTex(mGL, destTex.Texture());
        mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER,
                            LOCAL_GL_NEAREST);
        mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER,
                            LOCAL_GL_NEAREST);
        mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S,
                            LOCAL_GL_CLAMP_TO_EDGE);
        mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T,
                            LOCAL_GL_CLAMP_TO_EDGE);
        mGL->raw_fCopyTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, x, y, width,
                                 height, 0);
    }

    ScopedFramebufferForTexture tmpFB(mGL, destTex.Texture(), LOCAL_GL_TEXTURE_2D);
    ScopedBindFramebuffer bindFB(mGL, tmpFB.FB());
    mGL->raw_fCopyTexImage2D(target, level, internalformat, x, y, width, height, border);

    return true;
}

bool
SharedSurface_IOSurface::ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                    GLenum format, GLenum type, GLvoid* pixels)
{
    // Calling glReadPixels when an IOSurface is bound to the current framebuffer
    // can cause corruption in following glReadPixel calls (even if they aren't
    // reading from an IOSurface).
    // We workaround this by copying to a temporary texture, and doing the readback
    // from that.
    MOZ_ASSERT(mGL->IsCurrent());

    ScopedTexture destTex(mGL);
    {
        ScopedFramebufferForTexture srcFB(mGL, ProdTexture(), ProdTextureTarget());

        ScopedBindFramebuffer bindFB(mGL, srcFB.FB());
        ScopedBindTexture bindTex(mGL, destTex.Texture());
        mGL->raw_fCopyTexImage2D(LOCAL_GL_TEXTURE_2D, 0,
                                 mHasAlpha ? LOCAL_GL_RGBA : LOCAL_GL_RGB,
                                 x, y,
                                 width, height, 0);
    }

    ScopedFramebufferForTexture destFB(mGL, destTex.Texture());

    ScopedBindFramebuffer bindFB(mGL, destFB.FB());
    mGL->raw_fReadPixels(0, 0, width, height, format, type, pixels);
    return true;
}

bool
SharedSurface_IOSurface::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    bool isOpaque = false;
    *out_descriptor = layers::SurfaceDescriptorMacIOSurface(mIOSurf->GetIOSurfaceID(),
                                                            mIOSurf->GetContentsScaleFactor(),
                                                            isOpaque);
    return true;
}

bool
SharedSurface_IOSurface::ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface)
{
    MOZ_ASSERT(out_surface);
    mIOSurf->Lock();
    size_t bytesPerRow = mIOSurf->GetBytesPerRow();
    size_t ioWidth = mIOSurf->GetDevicePixelWidth();
    size_t ioHeight = mIOSurf->GetDevicePixelHeight();

    const unsigned char* ioData = (unsigned char*)mIOSurf->GetBaseAddress();
    gfx::DataSourceSurface::ScopedMap map(out_surface, gfx::DataSourceSurface::WRITE);
    if (!map.IsMapped()) {
        mIOSurf->Unlock();
        return false;
    }

    for (size_t i = 0; i < ioHeight; i++) {
        memcpy(map.GetData() + i * map.GetStride(),
               ioData + i * bytesPerRow, ioWidth * 4);
    }

    mIOSurf->Unlock();
    return true;
}

////////////////////////////////////////////////////////////////////////
// SurfaceFactory_IOSurface

/*static*/ gfx::IntSize
SurfaceFactory_IOSurface::MaxIOSurfaceSize()
{
    return gfx::IntSize::Truncate(MacIOSurface::GetMaxWidth(),
                                  MacIOSurface::GetMaxHeight());
}

UniquePtr<SharedSurface>
SurfaceFactory_IOSurface::NewSharedSurfaceImpl(const gfx::IntSize& size)
{
    if (size.width > mMaxDims.width ||
        size.height > mMaxDims.height)
    {
        return nullptr;
    }

    const auto& hasAlpha = true;
    RefPtr<MacIOSurface> ioSurf = MacIOSurface::CreateIOSurface(size.width, size.height,
                                                                1.0, hasAlpha);
    if (!ioSurf) {
        NS_WARNING("Failed to create MacIOSurface.");
        return nullptr;
    }

    mGL->MakeCurrent();

    const GLenum target = LOCAL_GL_TEXTURE_RECTANGLE_ARB;
    const GLuint tex = mGL->CreateTexture();
    {
        const ScopedBindTexture texture(mGL, tex, target);
        mGL->TexParams_SetClampNoMips(target);

        const auto& cglContext = GLContextCGL::Cast(mGL)->GetCGLContext();
        MOZ_ASSERT(cglContext);
        ioSurf->CGLTexImageIOSurface2D(cglContext);
    }

    const auto& mozFB = MozFramebuffer::CreateWith(gl, size, 0, mDepthStencil, target,
                                                   tex);
    if (!mozFB)
        return nullptr;

    return new SharedSurface_IOSurface(mGL, size, mozFB, ioSurf);
}

} // namespace gl
} // namespace mozilla
