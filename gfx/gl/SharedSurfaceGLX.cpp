/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceGLX.h"

#include "gfxXlibSurface.h"
#include "GLBlitHelper.h"
#include "GLContextProvider.h"
#include "GLContextGLX.h"
#include "GLXLibrary.h"
#include "MozFramebuffer.h"
#include "mozilla/gfx/SourceSurfaceCairo.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/ShadowLayerUtilsX11.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/X11Util.h"

namespace mozilla {
namespace gl {

/*static*/
UniquePtr<SharedSurface_GLXDrawable>
SharedSurface_GLXDrawable::Create(GLContext* const gl, const gfx::IntSize& size,
                                  const layers::TextureFlags flags,
                                  const bool inSameProcess)
{
    Display* display = DefaultXDisplay();
    Screen* screen = XDefaultScreenOfDisplay(display);
    Visual* visual = gfxXlibSurface::FindVisual(screen, gfx::SurfaceFormat::A8R8G8B8_UINT32);

    const RefPtr<gfxXlibSurface> surf = gfxXlibSurface::Create(screen, visual, size);
    if (!surf)
        return nullptr;

    const bool deallocateClient = bool(flags & layers::TextureFlags::DEALLOCATE_CLIENT);
    if (!deallocateClient) {
        surf->ReleasePixmap();
    }

    return AsUnique(new SharedSurface_GLXDrawable(gl, size, inSameProcess, surf));
}


SharedSurface_GLXDrawable::SharedSurface_GLXDrawable(GLContext* const gl,
                                                     const gfx::IntSize& size,
                                                     const bool inSameProcess,
                                                     gfxXlibSurface* const xlibSurface)
    : SharedSurface(SharedSurfaceType::GLXDrawable, gl, size, true, nullptr)
    , mXlibSurface(xlibSurface)
    , mInSameProcess(inSameProcess)
{ }

void
SharedSurface_GLXDrawable::ProducerReleaseImpl()
{
    mGL->MakeCurrent();
    mGL->fFlush();
}

void
SharedSurface_GLXDrawable::LockProdImpl()
{
    GLContextGLX::Cast(mGL)->OverrideDrawable(mXlibSurface->GetGLXPixmap());
}

void
SharedSurface_GLXDrawable::UnlockProdImpl()
{
    GLContextGLX::Cast(mGL)->RestoreDrawable();
}

bool
SharedSurface_GLXDrawable::CopyFromSameType(SharedSurface* const opaqueSrc)
{
    const auto src = (SharedSurface_GLXDrawable*)opaqueSrc;

    const auto srcPixmap = src->mXlibSurface->GetGLXPixmap();
    const auto destPixmap = mXlibSurface->GetGLXPixmap();

    const auto glxContext = (GLContextGLX*)mGL.get();
    MOZ_ALWAYS_TRUE( sGLXLibrary.fMakeContextCurrent(glxContext->mDisplay, destPixmap,
                                                     srcPixmap, glxContext->mContext) );
    mGL->BlitHelper()->BlitFramebufferToFramebuffer(0, 0, mSize, mSize);

    MOZ_ALWAYS_TRUE( mGL->MakeCurrent(true) );
    return true;
}

bool
SharedSurface_GLXDrawable::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    if (!mXlibSurface)
        return false;

    *out_descriptor = layers::SurfaceDescriptorX11(mXlibSurface, mInSameProcess);
    return true;
}

bool
SharedSurface_GLXDrawable::ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface)
{
    MOZ_ASSERT(out_surface);
    RefPtr<gfx::DataSourceSurface> dataSurf =
        new gfx::DataSourceSurfaceCairo(mXlibSurface->CairoSurface());

    gfx::DataSourceSurface::ScopedMap mapSrc(dataSurf, gfx::DataSourceSurface::READ);
    if (!mapSrc.IsMapped()) {
        return false;
    }

    gfx::DataSourceSurface::ScopedMap mapDest(out_surface, gfx::DataSourceSurface::WRITE);
    if (!mapDest.IsMapped()) {
        return false;
    }

    if (mapDest.GetStride() == mapSrc.GetStride()) {
        memcpy(mapDest.GetData(),
               mapSrc.GetData(),
               out_surface->GetSize().height * mapDest.GetStride());
    } else {
        for (int32_t i = 0; i < dataSurf->GetSize().height; i++) {
            memcpy(mapDest.GetData() + i * mapDest.GetStride(),
                   mapSrc.GetData() + i * mapSrc.GetStride(),
                   std::min(mapSrc.GetStride(), mapDest.GetStride()));
        }
    }

    return true;
}

////////////////////////////////////////

/*static*/ UniquePtr<SurfaceFactory_GLXDrawable>
SurfaceFactory_GLXDrawable::Create(GLContext* const gl, const bool depthStencil,
                                   layers::LayersIPCChannel* const allocator,
                                   const layers::TextureFlags flags)
{
    if (!sGLXLibrary.UseTextureFromPixmap())
        return nullptr;

    const bool configDepthStencil = bool(gl->mCreationFlags & CreateContextFlags::DEPTH_STENCIL_CONFIG);
    MOZ_ALWAYS_TRUE(depthStencil == configDepthStencil ||
                    gl->IsConfigDepthStencilFlexible());

    return AsUnique(new SurfaceFactory_GLXDrawable(gl, depthStencil, allocator, flags));
}

UniquePtr<SharedSurface>
SurfaceFactory_GLXDrawable::NewSharedSurfaceImpl(const gfx::IntSize& size)
{
    return SharedSurface_GLXDrawable::Create(mGL, size, mFlags,
                                             mAllocator->IsSameProcess());
}

} // namespace gl
} // namespace mozilla
