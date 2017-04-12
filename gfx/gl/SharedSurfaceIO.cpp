/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceIO.h"

#include "GLContextCGL.h"
#include "MozFramebuffer.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/gfx/MacIOSurface.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace gl {

SharedSurface_IOSurface::SharedSurface_IOSurface(GLContext* const gl,
                                                 const gfx::IntSize& size,
                                                 UniquePtr<MozFramebuffer> mozFB,
                                                 MacIOSurface* const ioSurf)
    : SharedSurface(SharedSurfaceType::IOSurface, gl, size, true, Move(mozFB))
    , mIOSurf(ioSurf)
{ }

void
SharedSurface_IOSurface::ProducerReleaseImpl()
{
    mGL->MakeCurrent();
    mGL->fFlush();
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

    auto mozFB = MozFramebuffer::CreateWith(mGL, size, 0, mDepthStencil, target, tex);
    if (!mozFB)
        return nullptr;

    return AsUnique(new SharedSurface_IOSurface(mGL, size, Move(mozFB), ioSurf));
}

} // namespace gl
} // namespace mozilla
