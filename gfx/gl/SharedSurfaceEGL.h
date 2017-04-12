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

class SharedSurface_EGLImage
    : public SharedSurface
{
public:
    static UniquePtr<SharedSurface_EGLImage> Create(GLContext* prodGL,
                                                    const GLFormats& formats,
                                                    const gfx::IntSize& size,
                                                    bool hasAlpha,
                                                    EGLContext context);

    static bool HasExtensions(GLLibraryEGL* egl, GLContext* gl);

protected:
    mutable Mutex mMutex;
    GLLibraryEGL* const mEGL;
    const GLFormats mFormats;
public:
    const EGLImage mImage;
protected:
    EGLSync mSync;

    SharedSurface_EGLImage(GLContext* gl,
                           GLuint tex,
                           const gfx::IntSize& size,
                           bool hasAlpha,
                           GLLibraryEGL* egl,
                           const GLFormats& formats,
                           EGLImage image);

    EGLDisplay Display() const;
    void UpdateProdTexture(const MutexAutoLock& curAutoLock);

public:
    virtual ~SharedSurface_EGLImage();

    virtual layers::TextureFlags GetTextureFlags() const override;

    virtual void LockProdImpl() override {}
    virtual void UnlockProdImpl() override {}

    virtual void ProducerAcquireImpl() override {}
    virtual void ProducerReleaseImpl() override;

    virtual void ProducerReadAcquireImpl() override;
    virtual void ProducerReadReleaseImpl() override {};

    // Implementation-specific functions below:
    // Returns texture and target
    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};



class SurfaceFactory_EGLImage
    : public SurfaceFactory
{
public:
    // Fallible:
    static UniquePtr<SurfaceFactory_EGLImage> Create(GLContext* prodGL,
                                                     const SurfaceCaps& caps,
                                                     const RefPtr<layers::LayersIPCChannel>& allocator,
                                                     const layers::TextureFlags& flags);

protected:
    const EGLContext mContext;

    SurfaceFactory_EGLImage(GLContext* prodGL, const SurfaceCaps& caps,
                            const RefPtr<layers::LayersIPCChannel>& allocator,
                            const layers::TextureFlags& flags,
                            EGLContext context)
        : SurfaceFactory(SharedSurfaceType::EGLImageShare, prodGL, caps, allocator, flags)
        , mContext(context)
    { }

public:
    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_EGLImage::Create(mGL, mFormats, size, mCaps.alpha, mContext);
    }
};

} // namespace gl

} /* namespace mozilla */

#endif /* SHARED_SURFACE_EGL_H_ */
