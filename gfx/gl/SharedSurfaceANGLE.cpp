/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceANGLE.h"

#include <d3d11.h>
#include "GLContextEGL.h"
#include "GLLibraryEGL.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc

namespace mozilla {
namespace gl {

// Returns `EGL_NO_SURFACE` (`0`) on error.
static EGLSurface
CreatePBufferSurface(GLLibraryEGL* egl,
                     EGLDisplay display,
                     EGLConfig config,
                     const gfx::IntSize& size)
{
    auto width = size.width;
    auto height = size.height;

    EGLint attribs[] = {
        LOCAL_EGL_WIDTH, width,
        LOCAL_EGL_HEIGHT, height,
        LOCAL_EGL_NONE
    };

    DebugOnly<EGLint> preCallErr = egl->fGetError();
    MOZ_ASSERT(preCallErr == LOCAL_EGL_SUCCESS);
    EGLSurface surface = egl->fCreatePbufferSurface(display, config, attribs);
    EGLint err = egl->fGetError();
    if (err != LOCAL_EGL_SUCCESS)
        return 0;

    return surface;
}

/*static*/ UniquePtr<SharedSurface_ANGLEShareHandle>
SharedSurface_ANGLEShareHandle::Create(GLContext* const gl, const EGLConfig config,
                                       const gfx::IntSize& size)
{
    GLLibraryEGL* egl = &sEGLLibrary;
    MOZ_ASSERT(egl);
    MOZ_ASSERT(egl->IsExtensionSupported(
               GLLibraryEGL::ANGLE_surface_d3d_texture_2d_share_handle));
    MOZ_ASSERT(config);

    EGLDisplay display = egl->Display();
    EGLSurface pbuffer = CreatePBufferSurface(egl, display, config, size);
    if (!pbuffer)
        return nullptr;

    HANDLE shareHandle = nullptr;
    IUnknown* maybeD3DTex = nullptr;
    MOZ_ALWAYS_TRUE( egl->fQuerySurfacePointerANGLE(display, pbuffer,
                                                    LOCAL_EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE,
                                                    &shareHandle) );
    MOZ_ALWAYS_TRUE( egl->fQuerySurfacePointerANGLE(display, pbuffer,
                                                    LOCAL_EGL_D3D_TEXTURE_ANGLE,
                                                    (void**)&maybeD3DTex) );
    MOZ_ASSERT(maybeD3DTex);

    ID3D11Texture2D* d3dTex = nullptr;
    if (maybeD3DTex) {
        maybeD3DTex->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&d3dTex);
    }

    if (!shareHandle || !d3dTex) {
        egl->fDestroySurface(egl->Display(), pbuffer);
        return nullptr;
    }

    return AsUnique(new SharedSurface_ANGLEShareHandle(gl, egl, size, pbuffer,
                                                       shareHandle, d3dTex));
}

static ID3D11DeviceContext*
D3DContext(GLLibraryEGL* egl)
{
    EGLDeviceEXT eglDevice = nullptr;
    egl->fQueryDisplayAttribEXT(egl->Display(), LOCAL_EGL_DEVICE_EXT,
                                (EGLAttrib*)&eglDevice);
    MOZ_ASSERT(eglDevice);

    ID3D11Device* d3dDevice = nullptr;
    egl->fQueryDeviceAttribEXT(eglDevice, LOCAL_EGL_D3D11_DEVICE_ANGLE,
                               (EGLAttrib*)&d3dDevice);
    MOZ_ASSERT(d3dDevice);

    ID3D11DeviceContext* d3dContext = nullptr;
    d3dDevice->GetImmediateContext(&d3dContext);
    MOZ_ASSERT(d3dContext);
    return d3dContext;
}

static IDXGIKeyedMutex*
KeyedMutex(GLLibraryEGL* const egl, const EGLSurface pbuffer)
{
    IDXGIKeyedMutex* keyedMutex = nullptr;
    MOZ_ALWAYS_TRUE( egl->fQuerySurfacePointerANGLE(egl->Display(), pbuffer,
                                                    LOCAL_EGL_DXGI_KEYED_MUTEX_ANGLE,
                                                    (void**)&keyedMutex) );
    return keyedMutex;
}

SharedSurface_ANGLEShareHandle::SharedSurface_ANGLEShareHandle(GLContext* const gl,
                                                               GLLibraryEGL* const egl,
                                                               const gfx::IntSize& size,
                                                               const EGLSurface pbuffer,
                                                               const HANDLE shareHandle,
                                                               ID3D11Texture2D* const angleTex)
    : SharedSurface(SharedSurfaceType::EGLSurfaceANGLE, gl, size, true, nullptr)
    , mEGL(egl)
    , mPBuffer(pbuffer)
    , mANGLETex(angleTex)
    , mANGLEKeyedMutex(KeyedMutex(egl, pbuffer))
    , mShareHandle(shareHandle)
    , mD3DContext(D3DContext(egl))
{
}

SharedSurface_ANGLEShareHandle::~SharedSurface_ANGLEShareHandle()
{
    mEGL->fDestroySurface(mEGL->Display(), mPBuffer);
}

void
SharedSurface_ANGLEShareHandle::LockProd()
{
    GLContextEGL::Cast(mGL)->SetEGLSurfaceOverride(mPBuffer);
}

void
SharedSurface_ANGLEShareHandle::ProducerAcquireImpl()
{
    if (mANGLEKeyedMutex) {
        HRESULT hr = mANGLEKeyedMutex->AcquireSync(0, 10000);
        if (hr == WAIT_TIMEOUT) {
            MOZ_CRASH("GFX: ANGLE share handle timeout");
        }
    }
}

void
SharedSurface_ANGLEShareHandle::ProducerReleaseImpl()
{
    if (mANGLEKeyedMutex) {
        // XXX: ReleaseSync() has an implicit flush of the D3D commands
        // whether we need Flush() or not depends on the ANGLE semantics.
        // For now, we'll just do it
        mGL->fFlush();
        mANGLEKeyedMutex->ReleaseSync(0);
        return;
    }
    mGL->fFinish();
}

void
SharedSurface_ANGLEShareHandle::ProducerReadAcquireImpl()
{
    ProducerAcquireImpl();
}

void
SharedSurface_ANGLEShareHandle::ProducerReadReleaseImpl()
{
    if (mANGLEKeyedMutex) {
        mANGLEKeyedMutex->ReleaseSync(0);
        return;
    }
}

////////////////////

bool
SharedSurface_ANGLEShareHandle::CopyFromSameType(SharedSurface* const opaqueSrc)
{
    const auto src = (SharedSurface_ANGLEShareHandle*)opaqueSrc;
    mD3DContext->CopyResource(mANGLETex, src->mANGLETex);
    return true;
}

////

bool
SharedSurface_ANGLEShareHandle::ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor)
{
    *out_descriptor = layers::SurfaceDescriptorD3D10((WindowsHandle)mShareHandle,
                                                     gfx::SurfaceFormat::B8G8R8A8, mSize);
    return true;
}

////////////////////

class ScopedLockTexture final
{
public:
    explicit ScopedLockTexture(ID3D11Texture2D* texture, bool* succeeded)
      : mIsLocked(false)
      , mTexture(texture)
    {
        MOZ_ASSERT(NS_IsMainThread(), "Must be on the main thread to use d3d11 immediate context");
        MOZ_ASSERT(mTexture);
        MOZ_ASSERT(succeeded);
        *succeeded = false;

        HRESULT hr;
        mTexture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mMutex));
        if (mMutex) {
            hr = mMutex->AcquireSync(0, 10000);
            if (hr == WAIT_TIMEOUT) {
                MOZ_CRASH("GFX: ANGLE scoped lock timeout");
            }

            if (FAILED(hr)) {
                NS_WARNING("Failed to lock the texture");
                return;
            }
        }

        RefPtr<ID3D11Device> device =
          gfx::DeviceManagerDx::Get()->GetContentDevice();
        if (!device) {
            return;
        }

        device->GetImmediateContext(getter_AddRefs(mDeviceContext));

        mTexture->GetDesc(&mDesc);
        mDesc.BindFlags = 0;
        mDesc.Usage = D3D11_USAGE_STAGING;
        mDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        mDesc.MiscFlags = 0;

        hr = device->CreateTexture2D(&mDesc, nullptr, getter_AddRefs(mCopiedTexture));

        if (FAILED(hr)) {
            return;
        }

        mDeviceContext->CopyResource(mCopiedTexture, mTexture);

        hr = mDeviceContext->Map(mCopiedTexture, 0, D3D11_MAP_READ, 0, &mSubresource);
        if (FAILED(hr)) {
            return;
        }

        *succeeded = true;
        mIsLocked = true;
    }

    ~ScopedLockTexture()
    {
        mDeviceContext->Unmap(mCopiedTexture, 0);
        if (mMutex) {
            HRESULT hr = mMutex->ReleaseSync(0);
            if (FAILED(hr)) {
                NS_WARNING("Failed to unlock the texture");
            }
        }
        mIsLocked = false;
    }

    bool mIsLocked;
    RefPtr<ID3D11Texture2D> mTexture;
    RefPtr<ID3D11Texture2D> mCopiedTexture;
    RefPtr<IDXGIKeyedMutex> mMutex;
    RefPtr<ID3D11DeviceContext> mDeviceContext;
    D3D11_TEXTURE2D_DESC mDesc;
    D3D11_MAPPED_SUBRESOURCE mSubresource;
};

bool
SharedSurface_ANGLEShareHandle::ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface)
{
    MOZ_ASSERT(out_surface);

    RefPtr<ID3D11Device> device =
      gfx::DeviceManagerDx::Get()->GetContentDevice();
    if (!device) {
        return false;
    }

    RefPtr<ID3D11Texture2D> tex;
    HRESULT hr = device->OpenSharedResource(mShareHandle,
                                            __uuidof(ID3D11Texture2D),
                                            (void**)(ID3D11Texture2D**)getter_AddRefs(tex));

    if (FAILED(hr)) {
        return false;
    }

    bool succeeded = false;
    ScopedLockTexture scopedLock(tex, &succeeded);
    if (!succeeded) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<uint8_t*>(scopedLock.mSubresource.pData);
    uint32_t srcStride = scopedLock.mSubresource.RowPitch;

    gfx::DataSourceSurface::ScopedMap map(out_surface, gfx::DataSourceSurface::WRITE);
    if (!map.IsMapped()) {
        return false;
    }

    if (map.GetStride() == srcStride) {
        memcpy(map.GetData(), data, out_surface->GetSize().height * map.GetStride());
    } else {
        const uint8_t bytesPerPixel = BytesPerPixel(out_surface->GetFormat());
        for (int32_t i = 0; i < out_surface->GetSize().height; i++) {
            memcpy(map.GetData() + i * map.GetStride(),
                   data + i * srcStride,
                   bytesPerPixel * out_surface->GetSize().width);
        }
    }

    DXGI_FORMAT srcFormat = scopedLock.mDesc.Format;
    MOZ_ASSERT(srcFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
               srcFormat == DXGI_FORMAT_B8G8R8X8_UNORM ||
               srcFormat == DXGI_FORMAT_R8G8B8A8_UNORM);
    bool isSrcRGB = srcFormat == DXGI_FORMAT_R8G8B8A8_UNORM;

    gfx::SurfaceFormat destFormat = out_surface->GetFormat();
    MOZ_ASSERT(destFormat == gfx::SurfaceFormat::R8G8B8X8 ||
               destFormat == gfx::SurfaceFormat::R8G8B8A8 ||
               destFormat == gfx::SurfaceFormat::B8G8R8X8 ||
               destFormat == gfx::SurfaceFormat::B8G8R8A8);
    bool isDestRGB = destFormat == gfx::SurfaceFormat::R8G8B8X8 ||
                     destFormat == gfx::SurfaceFormat::R8G8B8A8;

    if (isSrcRGB != isDestRGB) {
        SwapRAndBComponents(out_surface);
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Factory

/*static*/ UniquePtr<SurfaceFactory_ANGLEShareHandle>
SurfaceFactory_ANGLEShareHandle::Create(GLContext* const gl, const bool depthStencil,
                                        layers::LayersIPCChannel* allocator,
                                        const layers::TextureFlags flags)
{
    // Enable surface sharing only if ANGLE and compositing devices
    // are both WARP or both not WARP
    gfx::DeviceManagerDx* const dm = gfx::DeviceManagerDx::Get();
    if (!gl->IsANGLE() ||
        gl->IsWARP() != dm->IsWARP() ||
        !dm->TextureSharingWorks())
    {
        return nullptr;
    }

    const auto& egl = &sEGLLibrary;
    if (!egl)
        return nullptr;

    const auto ext = GLLibraryEGL::ANGLE_surface_d3d_texture_2d_share_handle;
    if (!egl->IsExtensionSupported(ext))
        return nullptr;

    const auto& config = GLContextEGL::Cast(gl)->mConfig;

    return AsUnique(new SurfaceFactory_ANGLEShareHandle(gl, depthStencil, allocator,
                                                        flags, egl, config));
}

} /* namespace gl */
} /* namespace mozilla */
