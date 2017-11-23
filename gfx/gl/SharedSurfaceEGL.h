/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_EGL_H_
#define SHARED_SURFACE_EGL_H_

#include "mozilla/Attributes.h"
#include "mozilla/Mutex.h"
#include "SharedSurface.h"

#ifdef MOZ_WIDGET_ANDROID
#include "GeneratedJNIWrappers.h"
#include "AndroidNativeWindow.h"
#endif

namespace mozilla {
namespace gl {

class GLContext;
class GLLibraryEGL;

class SharedSurface_EGLImage final
    : public SharedSurface
{
    GLLibraryEGL* const mEGL;
public:
    const EGLImage mImage;
private:
    EGLSync mSync;

public:
    static UniquePtr<SharedSurface_EGLImage> Create(GLContext* gl,
                                                    const gfx::IntSize& size,
                                                    bool depthStencil,
                                                    EGLContext context);

private:
    SharedSurface_EGLImage(GLContext* gl, const gfx::IntSize& size,
                           UniquePtr<MozFramebuffer> mozFB, GLLibraryEGL* egl,
                           EGLImage image);

    virtual ~SharedSurface_EGLImage();

    virtual layers::TextureFlags GetTextureFlags() const override {
      return layers::TextureFlags::DEALLOCATE_CLIENT;
    }

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual void ProducerReadAcquireImpl() override;
    virtual void ProducerReadReleaseImpl() override {};

    // Implementation-specific functions below:
    // Returns texture and target
    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};



class SurfaceFactory_EGLImage final
    : public SurfaceFactory
{
    const EGLContext mContext;

public:
    static UniquePtr<SurfaceFactory_EGLImage> Create(GLContext* gl, bool depthStencil,
                                                     layers::LayersIPCChannel* allocator,
                                                     layers::TextureFlags flags);

private:
    SurfaceFactory_EGLImage(GLContext* const gl, const bool depthStencil,
                            layers::LayersIPCChannel* const allocator,
                            const layers::TextureFlags flags, const EGLContext context)
        : SurfaceFactory(SharedSurfaceType::EGLImageShare, gl, depthStencil, allocator,
                         flags)
        , mContext(context)
    { }

    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_EGLImage::Create(mGL, size, mDepthStencil, mContext);
    }
};

#ifdef MOZ_WIDGET_ANDROID

class SharedSurface_SurfaceTexture final
    : public SharedSurface
{
    java::GeckoSurface::GlobalRef mSurface;
    EGLSurface mEglSurface;
    EGLSurface mOverriddenSurface = 0;

public:
    static UniquePtr<SharedSurface_SurfaceTexture> Create(GLContext* gl,
                                                          const gfx::IntSize& size,
														  bool depthStencil,
                                                          java::GeckoSurface::Param surface);

private:
    SharedSurface_SurfaceTexture(GLContext* gl, const gfx::IntSize& size,
								 UniquePtr<MozFramebuffer> mozFB,
                                 java::GeckoSurface::Param surface,
                                 EGLSurface eglSurface);

    virtual ~SharedSurface_SurfaceTexture();

    virtual layers::TextureFlags GetTextureFlags() const override {
      return layers::TextureFlags::DEALLOCATE_CLIENT;
    }

    virtual void LockProd() override;
    virtual void UnlockProd() override;

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override {}

    virtual void ProducerReadAcquireImpl() override {}
    virtual void ProducerReadReleaseImpl() override {}

    // Implementation-specific functions below:
    // Returns texture and target
    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override { return false; }

    virtual void Commit() override;

    virtual void WaitForBufferOwnership() override;

public:
    java::GeckoSurface::Param JavaSurface() { return mSurface; }
};



class SurfaceFactory_SurfaceTexture final
    : public SurfaceFactory
{
public:
    SurfaceFactory_SurfaceTexture(GLContext* const gl, const bool depthStencil,
                                  layers::LayersIPCChannel* const allocator,
                                  const layers::TextureFlags flags)
        : SurfaceFactory(SharedSurfaceType::AndroidSurfaceTexture, gl, depthStencil,
                         allocator, flags)
    { }

private:
    virtual UniquePtr<SharedSurface> NewSharedSurfaceImpl(const gfx::IntSize& size) override;
};

#endif // MOZ_WIDGET_ANDROID

} // namespace gl
} // namespace mozilla

#endif // SHARED_SURFACE_EGL_H_
