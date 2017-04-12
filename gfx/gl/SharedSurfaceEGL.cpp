/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceEGL.h"

#include "GLBlitHelper.h"
#include "GLContextEGL.h"
#include "GLLibraryEGL.h"
#include "GLReadTexImageHelper.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "SharedSurface.h"
#include "TextureGarbageBin.h"

namespace mozilla {
namespace gl {

/*static*/ UniquePtr<SharedSurface_EGLImage>
SharedSurface_EGLImage::Create(GLContext* prodGL,
                               const GLFormats& formats,
                               const gfx::IntSize& size,
                               bool hasAlpha,
                               EGLContext context)
{
    GLLibraryEGL* egl = &sEGLLibrary;
    MOZ_ASSERT(egl);
    MOZ_ASSERT(context);

    if (!HasExtensions(egl, prodGL))
        return nullptr;

    MOZ_ALWAYS_TRUE(prodGL->MakeCurrent());
    GLuint prodTex = CreateTextureForOffscreen(prodGL, formats, size);
    if (!prodTex)
        return nullptr;

    EGLClientBuffer buffer = reinterpret_cast<EGLClientBuffer>(uintptr_t(prodTex));
    EGLImage image = egl->fCreateImage(egl->Display(), context,
                                       LOCAL_EGL_GL_TEXTURE_2D, buffer,
                                       nullptr);
    if (!image) {
        prodGL->fDeleteTextures(1, &prodTex);
        return nullptr;
    }

    return UniquePtr<SharedSurface_EGLImage>(
        new SharedSurface_EGLImage(prodGL, prodTex, size, hasAlpha, egl, formats, image));
}

bool
SharedSurface_EGLImage::HasExtensions(GLLibraryEGL* egl, GLContext* gl)
{
    return egl->HasKHRImageBase() &&
           egl->IsExtensionSupported(GLLibraryEGL::KHR_gl_texture_2D_image) &&
           (gl->IsExtensionSupported(GLContext::OES_EGL_image_external) ||
            gl->IsExtensionSupported(GLContext::OES_EGL_image));
}

SharedSurface_EGLImage::SharedSurface_EGLImage(GLContext* gl,
                                               GLuint tex,
                                               const gfx::IntSize& size,
                                               bool hasAlpha,
                                               GLLibraryEGL* egl,
                                               const GLFormats& formats,
                                               EGLImage image)
    : SharedSurface(SharedSurfaceType::EGLImageShare,
                    gl,
                    LOCAL_GL_TEXTURE_2D, tex, true, 0,
                    size,
                    hasAlpha,
                    false) // Can't recycle, as mSync changes never update TextureHost.
    , mMutex("SharedSurface_EGLImage mutex")
    , mEGL(egl)
    , mFormats(formats)
    , mImage(image)
    , mSync(0)
{}

SharedSurface_EGLImage::~SharedSurface_EGLImage()
{
    mEGL->fDestroyImage(Display(), mImage);

    if (mSync) {
        // We can't call this unless we have the ext, but we will always have
        // the ext if we have something to destroy.
        mEGL->fDestroySync(Display(), mSync);
        mSync = 0;
    }
}

layers::TextureFlags
SharedSurface_EGLImage::GetTextureFlags() const
{
    return layers::TextureFlags::DEALLOCATE_CLIENT;
}

void
SharedSurface_EGLImage::ProducerReleaseImpl()
{
    MutexAutoLock lock(mMutex);
    mGL->MakeCurrent();

    if (mEGL->IsExtensionSupported(GLLibraryEGL::KHR_fence_sync) &&
        mGL->IsExtensionSupported(GLContext::OES_EGL_sync))
    {
        if (mSync) {
            MOZ_RELEASE_ASSERT(false, "GFX: Non-recycleable should not Fence twice.");
            MOZ_ALWAYS_TRUE( mEGL->fDestroySync(Display(), mSync) );
            mSync = 0;
        }

        mSync = mEGL->fCreateSync(Display(),
                                  LOCAL_EGL_SYNC_FENCE,
                                  nullptr);
        if (mSync) {
            mGL->fFlush();
            return;
        }
    }

    MOZ_ASSERT(!mSync);
    mGL->fFinish();
}

void
SharedSurface_EGLImage::ProducerReadAcquireImpl()
{
    // Wait on the fence, because presumably we're going to want to read this surface
    if (mSync) {
        mEGL->fClientWaitSync(Display(), mSync, 0, LOCAL_EGL_FOREVER);
    }
}

EGLDisplay
SharedSurface_EGLImage::Display() const
{
    return mEGL->Display();
}

bool
SharedSurface_EGLImage::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    *out_descriptor = layers::EGLImageDescriptor((uintptr_t)mImage, (uintptr_t)mSync,
                                                 mSize, mHasAlpha);
    return true;
}

bool
SharedSurface_EGLImage::ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface)
{
    MOZ_ASSERT(out_surface);
    MOZ_ASSERT(NS_IsMainThread());
    return sEGLLibrary.ReadbackEGLImage(mImage, out_surface);
}

////////////////////////////////////////////////////////////////////////

/*static*/ UniquePtr<SurfaceFactory_EGLImage>
SurfaceFactory_EGLImage::Create(GLContext* prodGL, const SurfaceCaps& caps,
                                const RefPtr<layers::LayersIPCChannel>& allocator,
                                const layers::TextureFlags& flags)
{
    EGLContext context = GLContextEGL::Cast(prodGL)->mContext;

    typedef SurfaceFactory_EGLImage ptrT;
    UniquePtr<ptrT> ret;

    GLLibraryEGL* egl = &sEGLLibrary;
    if (SharedSurface_EGLImage::HasExtensions(egl, prodGL)) {
        ret.reset( new ptrT(prodGL, caps, allocator, flags, context) );
    }

    return Move(ret);
}

} // namespace gl

} /* namespace mozilla */
