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
    static UniquePtr<SharedSurface_Basic> Create(GLContext* gl,
                                                 const GLFormats& formats,
                                                 const gfx::IntSize& size,
                                                 bool hasAlpha);

    static UniquePtr<SharedSurface_Basic> Wrap(GLContext* gl,
                                               const gfx::IntSize& size,
                                               bool hasAlpha,
                                               GLuint tex);

protected:
    SharedSurface_Basic(GLContext* gl,
                        const gfx::IntSize& size,
                        bool hasAlpha,
                        GLuint tex,
                        bool ownsTex);

public:
    virtual void LockProdImpl() override {}
    virtual void UnlockProdImpl() override {}

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override {}

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override {
        MOZ_CRASH("GFX: ToSurfaceDescriptor");
        return false;
    }
};

class SurfaceFactory_Basic
    : public SurfaceFactory
{
public:
    SurfaceFactory_Basic(GLContext* gl, const SurfaceCaps& caps,
                         const layers::TextureFlags& flags);

    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_Basic::Create(mGL, mFormats, size, mCaps.alpha);
    }
};


// Using shared GL textures:
class SharedSurface_GLTexture
    : public SharedSurface
{
public:
    static UniquePtr<SharedSurface_GLTexture> Create(GLContext* prodGL,
                                                     const GLFormats& formats,
                                                     const gfx::IntSize& size,
                                                     bool hasAlpha);

protected:
    GLsync mSync;

    SharedSurface_GLTexture(GLContext* prodGL,
                            const gfx::IntSize& size,
                            bool hasAlpha,
                            GLuint tex)
        : SharedSurface(SharedSurfaceType::SharedGLTexture,
                        prodGL,
                        LOCAL_GL_TEXTURE_2D, tex, true, 0,
                        size,
                        hasAlpha, true)
        , mSync(0)
    {
    }

public:
    virtual ~SharedSurface_GLTexture();

    virtual void LockProdImpl() override {}
    virtual void UnlockProdImpl() override {}

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;
};

class SurfaceFactory_GLTexture
    : public SurfaceFactory
{
public:
    SurfaceFactory_GLTexture(GLContext* prodGL,
                             const SurfaceCaps& caps,
                             const RefPtr<layers::LayersIPCChannel>& allocator,
                             const layers::TextureFlags& flags)
        : SurfaceFactory(SharedSurfaceType::SharedGLTexture, prodGL, caps, allocator, flags)
    {
    }

    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_GLTexture::Create(mGL, mFormats, size, mCaps.alpha);
    }
};

} // namespace gl

} /* namespace mozilla */

#endif /* SHARED_SURFACE_GL_H_ */
