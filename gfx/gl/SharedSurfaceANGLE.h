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

class SharedSurface_ANGLEShareHandle
    : public SharedSurface
{
public:
    static UniquePtr<SharedSurface_ANGLEShareHandle> Create(GLContext* gl,
                                                            EGLConfig config,
                                                            const gfx::IntSize& size,
                                                            bool hasAlpha);

protected:
    GLLibraryEGL* const mEGL;
    const EGLSurface mPBuffer;
public:
    const HANDLE mShareHandle;
    const RefPtr<ID3D11Texture2D> mD3DTex;
    const RefPtr<ID3D11DeviceContext> mD3DContext;
protected:
    RefPtr<IDXGIKeyedMutex> mKeyedMutex;

    SharedSurface_ANGLEShareHandle(GLContext* gl,
                                   GLLibraryEGL* egl,
                                   const gfx::IntSize& size,
                                   bool hasAlpha,
                                   EGLSurface pbuffer,
                                   HANDLE shareHandle,
                                   ID3D11Texture2D* d3dTex);

    EGLDisplay Display();

public:
    virtual ~SharedSurface_ANGLEShareHandle();

    virtual void LockProdImpl() override;
    virtual void UnlockProdImpl() override;

    virtual void ProducerAcquireImpl() override;
    virtual void ProducerReleaseImpl() override;
    virtual void ProducerReadAcquireImpl() override;
    virtual void ProducerReadReleaseImpl() override;

    virtual bool CopyFromSameType(SharedSurface* src) override;

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) override;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) override;
};



class SurfaceFactory_ANGLEShareHandle
    : public SurfaceFactory
{
protected:
    GLContext* const mProdGL;
    GLLibraryEGL* const mEGL;
    const EGLConfig mConfig;

public:
    static UniquePtr<SurfaceFactory_ANGLEShareHandle> Create(GLContext* gl,
                                                             const SurfaceCaps& caps,
                                                             const RefPtr<layers::LayersIPCChannel>& allocator,
                                                             const layers::TextureFlags& flags);

protected:
    SurfaceFactory_ANGLEShareHandle(GLContext* gl, const SurfaceCaps& caps,
                                    const RefPtr<layers::LayersIPCChannel>& allocator,
                                    const layers::TextureFlags& flags, GLLibraryEGL* egl,
                                    EGLConfig config);

    virtual UniquePtr<SharedSurface>
    NewSharedSurfaceImpl(const gfx::IntSize& size) override {
        return SharedSurface_ANGLEShareHandle::Create(mProdGL, mConfig, size,
                                                      mCaps.alpha);
    }
};

} /* namespace gfx */
} /* namespace mozilla */

#endif /* SHARED_SURFACE_ANGLE_H_ */
