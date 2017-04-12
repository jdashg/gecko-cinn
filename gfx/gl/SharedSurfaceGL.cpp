/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceGL.h"

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "mozilla/gfx/2D.h"
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace gl {

using gfx::IntSize;
using gfx::SurfaceFormat;

/*static*/ UniquePtr<SharedSurface_Basic>
SharedSurface_Basic::Create(GLContext* gl, const IntSize& size, const bool depthStencil)
{
    gl->MakeCurrent();

    auto mozFB = MozFramebuffer::Create(gl, size, 0, depthStencil);
    if (!mozFB)
        return nullptr;

    return AsUnique(new SharedSurface_Basic(gl, size, Move(mozFB)));
}

SharedSurface_Basic::SharedSurface_Basic(GLContext* const gl, const IntSize& size,
                                         UniquePtr<MozFramebuffer> mozFB)
    : SharedSurface(SharedSurfaceType::Basic, gl, size, true, Move(mozFB))
{ }

////////////////////////////////////////////////////////////////////////
// SharedSurface_GLTexture

/*static*/ UniquePtr<SharedSurface_GLTexture>
SharedSurface_GLTexture::Create(GLContext* const gl, const IntSize& size,
                                const bool depthStencil)
{
    gl->MakeCurrent();

    auto mozFB = MozFramebuffer::Create(gl, size, 0, depthStencil);
    if (!mozFB)
        return nullptr;

    return AsUnique(new SharedSurface_GLTexture(gl, size, Move(mozFB)));
}

SharedSurface_GLTexture::SharedSurface_GLTexture(GLContext* const gl, const IntSize& size,
                                                 UniquePtr<MozFramebuffer> mozFB)
    : SharedSurface(SharedSurfaceType::SharedGLTexture, gl, size, true, Move(mozFB))
{ }

SharedSurface_GLTexture::~SharedSurface_GLTexture()
{
    const auto gl = mGL.get();
    if (!gl || !gl->MakeCurrent())
        return;

    if (mSync) {
        gl->fDeleteSync(mSync);
    }
}

void
SharedSurface_GLTexture::ProducerReleaseImpl()
{
    mGL->MakeCurrent();

    if (mGL->IsSupported(GLFeature::sync)) {
        if (mSync) {
            mGL->fDeleteSync(mSync);
            mSync = 0;
        }

        mSync = mGL->fFenceSync(LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (mSync) {
            mGL->fFlush();
            return;
        }
    }
    MOZ_ASSERT(!mSync);

    mGL->fFinish();
}

bool
SharedSurface_GLTexture::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    const bool hasAlpha = true;
    *out_descriptor = layers::SurfaceDescriptorSharedGLTexture(mMozFB->ColorTex(),
                                                               mMozFB->mColorTarget,
                                                               uintptr_t(mSync), mSize,
                                                               hasAlpha);

    // Transfer ownership of the fence to the host
    mSync = 0;
    return true;
}

} // namespace gl
} /* namespace mozilla */
