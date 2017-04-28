/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_GLX_H_
#define SHARED_SURFACE_GLX_H_

#include "SharedSurface.h"
#include "mozilla/RefPtr.h"

class gfxXlibSurface;

namespace mozilla {
namespace gl {

class SharedSurface_GLXDrawable final
    : public SharedSurface
{
    const RefPtr<gfxXlibSurface> mXlibSurface;
    const bool mInSameProcess;

public:
    static UniquePtr<SharedSurface_GLXDrawable> Create(GLContext* gl,
                                                       const gfx::IntSize& size,
                                                       layers::TextureFlags flags,
                                                       bool inSameProcess);
private:
    SharedSurface_GLXDrawable(GLContext* gl, const gfx::IntSize& size,
                              bool inSameProcess, gfxXlibSurface* xlibSurface);

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual void LockProdImpl() override;
    virtual void UnlockProdImpl() override;

    virtual bool CopyFromSameType(SharedSurface* src) override;

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};

class SurfaceFactory_GLXDrawable final
    : public SurfaceFactory
{
public:
    SurfaceFactory_GLXDrawable(GLContext* const gl, const bool depthStencil,
                               layers::LayersIPCChannel* const allocator,
                               const layers::TextureFlags flags)
        : SurfaceFactory(SharedSurfaceType::GLXDrawable, gl, depthStencil, allocator,
                         flags)
    {
        const bool contextDepthStencil = (gl->mCreateFlags & CreateContextFlags::DEPTH_STENCIL_CONFIG);
        MOZ_ALWAYS_TRUE(mDepthStencil == contextDepthStencil ||
                        gl->IsConfigDepthStencilFlexible());
    }

private:
    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override;
};

} // namespace gl
} // namespace mozilla

#endif // SHARED_SURFACE_GLX_H_
