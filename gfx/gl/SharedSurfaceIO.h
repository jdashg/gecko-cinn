/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACEIO_H_
#define SHARED_SURFACEIO_H_

#include "mozilla/RefPtr.h"
#include "SharedSurface.h"

class MacIOSurface;

namespace mozilla {
namespace gl {

class SharedSurface_IOSurface final : public SharedSurface
{
public:
    const RefPtr<MacIOSurface> mIOSurf;

    SharedSurface_IOSurface(GLContext* gl, const gfx::IntSize& size,
                            UniquePtr<MozFramebuffer>&& mozFB, MacIOSurface* ioSurf);
private:
    virtual void LockProdImpl() override { }
    virtual void UnlockProdImpl() override { }

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual bool CopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                GLint x, GLint y, GLsizei width, GLsizei height,
                                GLint border) override;
    virtual bool ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, GLvoid* pixels) override;

    virtual bool NeedsIndirectReads() const override { return true; }
    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;
    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};

class SurfaceFactory_IOSurface final : public SurfaceFactory
{
    const gfx::IntSize mMaxDims;

    static gfx::IntSize MaxIOSurfaceSize();

public:
    SurfaceFactory_IOSurface(GLContext* const gl, const bool depthStencil,
                             layers::LayersIPCChannel* const allocator,
                             const layers::TextureFlags flags)
        : SurfaceFactory(SharedSurfaceType::IOSurface, gl, depthStencil, allocator, flags)
        , mMaxDims(MaxIOSurfaceSize())
    { }

private:
    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override;
};

} // namespace gl

} /* namespace mozilla */

#endif /* SHARED_SURFACEIO_H_ */
