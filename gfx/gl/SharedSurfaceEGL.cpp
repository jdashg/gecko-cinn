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

static bool
HasExtensions(GLLibraryEGL* const egl, GLContext* const gl)
{
    return egl->HasKHRImageBase() &&
           egl->IsExtensionSupported(GLLibraryEGL::KHR_gl_texture_2D_image) &&
           (gl->IsExtensionSupported(GLContext::OES_EGL_image_external) ||
            gl->IsExtensionSupported(GLContext::OES_EGL_image));
}

/*static*/ UniquePtr<SharedSurface_EGLImage>
SharedSurface_EGLImage::Create(GLContext* const gl, const gfx::IntSize& size,
                               const bool depthStencil, const EGLContext context)
{
    const auto& egl = &sEGLLibrary;
    MOZ_ASSERT(egl);
    MOZ_ASSERT(context);

    if (!HasExtensions(egl, gl))
        return nullptr;

    gl->MakeCurrent();
    auto mozFB = MozFramebuffer::Create(gl, size, 0, depthStencil);
    if (!mozFB)
        return nullptr;

    const auto& tex = EGLClientBuffer(uintptr_t(mozFB->ColorTex()));
    EGLImage image = egl->fCreateImage(egl->Display(), context,
                                       LOCAL_EGL_GL_TEXTURE_2D, tex,
                                       nullptr);
    if (!image)
        return nullptr;

    return AsUnique(new SharedSurface_EGLImage(gl, size, Move(mozFB), egl, image));
}

SharedSurface_EGLImage::SharedSurface_EGLImage(GLContext* const gl,
                                               const gfx::IntSize& size,
                                               UniquePtr<MozFramebuffer> mozFB,
                                               GLLibraryEGL* const egl,
                                               const EGLImage image)
    : SharedSurface(SharedSurfaceType::EGLImageShare, gl, size,
                    false, // Can't recycle, as mSync changes never updates TextureHost.
                    Move(mozFB))
    , mEGL(egl)
    , mImage(image)
    , mSync(0)
{ }

SharedSurface_EGLImage::~SharedSurface_EGLImage()
{
    mEGL->fDestroyImage(mEGL->Display(), mImage);

    if (mSync) {
        // We can't call this unless we have the ext, but we will always have
        // the ext if we have something to destroy.
        mEGL->fDestroySync(mEGL->Display(), mSync);
        mSync = 0;
    }
}

void
SharedSurface_EGLImage::ProducerReleaseImpl()
{
    mGL->MakeCurrent();

    if (mEGL->IsExtensionSupported(GLLibraryEGL::KHR_fence_sync) &&
        mGL->IsExtensionSupported(GLContext::OES_EGL_sync))
    {
        if (mSync) {
            MOZ_RELEASE_ASSERT(false, "GFX: Non-recycleable should not Fence twice.");
            MOZ_ALWAYS_TRUE( mEGL->fDestroySync(mEGL->Display(), mSync) );
            mSync = 0;
        }

        mSync = mEGL->fCreateSync(mEGL->Display(),
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
        mEGL->fClientWaitSync(mEGL->Display(), mSync, 0, LOCAL_EGL_FOREVER);
    }
}

bool
SharedSurface_EGLImage::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    const bool hasAlpha = true;
    *out_descriptor = layers::EGLImageDescriptor(uintptr_t(mImage), uintptr_t(mSync),
                                                 mSize, hasAlpha);
    return true;
}

bool
SharedSurface_EGLImage::ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface)
{
    MOZ_ASSERT(out_surface);
    MOZ_ASSERT(NS_IsMainThread());
    return mEGL->ReadbackEGLImage(mImage, out_surface);
}

////////////////////////////////////////////////////////////////////////

/*static*/ UniquePtr<SurfaceFactory_EGLImage>
SurfaceFactory_EGLImage::Create(GLContext* const gl, const bool depthStencil,
                                layers::LayersIPCChannel* const allocator,
                                const layers::TextureFlags flags)
{
    const auto& egl = &sEGLLibrary;
    if (!HasExtensions(egl, gl))
        return nullptr;

    const auto& context = GLContextEGL::Cast(gl)->mContext;
    return AsUnique(new SurfaceFactory_EGLImage(gl, depthStencil, allocator, flags,
                                                context));
}

} // namespace gl

} /* namespace mozilla */
