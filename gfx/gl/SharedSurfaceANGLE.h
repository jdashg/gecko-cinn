/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_ANGLE_H_
#define SHARED_SURFACE_ANGLE_H_

#include <windows.h>
#include "SharedSurface.h"

struct IDXGIKeyedMutex;
struct ID3D11Texture2D;
struct ID3D11DeviceContext;

namespace mozilla {
namespace gl {

class GLContext;
class GLLibraryEGL;

class SharedSurface_ANGLEShareHandle final
    : public SharedSurface
{
    GLLibraryEGL* const mEGL;
    const EGLSurface mPBuffer;
    const RefPtr<IDXGIKeyedMutex> mANGLEKeyedMutex;
    const RefPtr<ID3D11Texture2D> mANGLETex;
    const HANDLE mShareHandle;
    const RefPtr<ID3D11DeviceContext> mD3DContext;

public:
    static UniquePtr<SharedSurface_ANGLEShareHandle> Create(GLContext* gl,
                                                            EGLConfig config,
                                                            const gfx::IntSize& size);
private:
    SharedSurface_ANGLEShareHandle(GLContext* gl, GLLibraryEGL* egl,
                                   const gfx::IntSize& size, EGLSurface pbuffer,
                                   HANDLE shareHandle, ID3D11Texture2D* d3dTex);
public:
    virtual ~SharedSurface_ANGLEShareHandle() override;
private:
    virtual void LockProd() override;
    virtual void UnlockProd() override { }

    virtual void ProducerAcquireImpl() override;
    virtual void ProducerReleaseImpl() override;
    virtual void ProducerReadAcquireImpl() override;
    virtual void ProducerReadReleaseImpl() override;

    virtual bool CopyFromSameType(SharedSurface* src) override;

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};

////

class SurfaceFactory_ANGLEShareHandle final
    : public SurfaceFactory
{
    GLLibraryEGL* const mEGL;
    const EGLConfig mConfig;

public:
    static UniquePtr<SurfaceFactory_ANGLEShareHandle> Create(GLContext* gl,
                                                             bool depthStencil,
                                                             layers::LayersIPCChannel* allocator,
                                                             layers::TextureFlags flags);

private:
    SurfaceFactory_ANGLEShareHandle(GLContext* const gl, const bool depthStencil,
                                    layers::LayersIPCChannel* const allocator,
                                    const layers::TextureFlags flags,
                                    GLLibraryEGL* const egl, const EGLConfig config)
        : SurfaceFactory(SharedSurfaceType::EGLSurfaceANGLE, gl, depthStencil, allocator,
                         flags)
        , mEGL(egl)
        , mConfig(config)
    { }

    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_ANGLEShareHandle::Create(mGL, mConfig, size);
    }
};

} /* namespace gfx */
} /* namespace mozilla */

#endif /* SHARED_SURFACE_ANGLE_H_ */
