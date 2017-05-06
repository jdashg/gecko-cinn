/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_GL_H_
#define SHARED_SURFACE_GL_H_

#include "ScopedGLHelpers.h"
#include "SharedSurface.h"
#include "SurfaceTypes.h"
#include "GLContextTypes.h"
#include "gfxTypes.h"
#include "mozilla/Mutex.h"

#include <queue>

namespace mozilla {
    namespace gl {
        class GLContext;
    } // namespace gl
    namespace gfx {
        class DataSourceSurface;
    } // namespace gfx
} // namespace mozilla

namespace mozilla {
namespace gl {

// For readback and bootstrapping:
class SharedSurface_Basic
    : public SharedSurface
{
public:
    static UniquePtr<SharedSurface_Basic> Create(GLContext* gl, const gfx::IntSize& size,
                                                 bool depthStencil);
private:
    SharedSurface_Basic(GLContext* gl, const gfx::IntSize& size,
                        UniquePtr<MozFramebuffer> mozFB);

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override {}

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const) override {
        MOZ_CRASH("GFX: SharedSurface_Basic::ToSurfaceDescriptor");
        return false;
    }
};

class SurfaceFactory_Basic final
    : public SurfaceFactory
{
public:
    SurfaceFactory_Basic(GLContext* const gl, const bool depthStencil,
                         layers::LayersIPCChannel* const allocator,
                         const layers::TextureFlags flags)
        : SurfaceFactory(SharedSurfaceType::Basic, gl, depthStencil, allocator, flags)
    { }

private:
    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_Basic::Create(mGL, size, mDepthStencil);
    }
};


// Using shared GL textures:
class SharedSurface_GLTexture final
    : public SharedSurface
{
    GLsync mSync;

public:
    static UniquePtr<SharedSurface_GLTexture> Create(GLContext* gl,
                                                     const gfx::IntSize& size,
                                                     bool depthStencil);
private:
    SharedSurface_GLTexture(GLContext* gl, const gfx::IntSize& size,
                            UniquePtr<MozFramebuffer> mozFB);
public:
    virtual ~SharedSurface_GLTexture() override;
private:
    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;
};

class SurfaceFactory_GLTexture final
    : public SurfaceFactory
{
public:
    SurfaceFactory_GLTexture(GLContext* const gl, const bool depthStencil,
                             layers::LayersIPCChannel* const allocator,
                             const layers::TextureFlags flags)
        : SurfaceFactory(SharedSurfaceType::SharedGLTexture, gl, depthStencil, allocator,
                         flags)
    { }

private:
    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_GLTexture::Create(mGL, size, mDepthStencil);
    }
};

} // namespace gl

} /* namespace mozilla */

#endif /* SHARED_SURFACE_GL_H_ */
