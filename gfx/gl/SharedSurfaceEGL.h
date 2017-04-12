/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_EGL_H_
#define SHARED_SURFACE_EGL_H_

#include "mozilla/Attributes.h"
#include "mozilla/Mutex.h"
#include "SharedSurface.h"

namespace mozilla {
namespace gl {

class GLContext;
class GLLibraryEGL;
class TextureGarbageBin;

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
public:
    virtual ~SharedSurface_EGLImage();

private:
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

} // namespace gl

} /* namespace mozilla */

#endif /* SHARED_SURFACE_EGL_H_ */
