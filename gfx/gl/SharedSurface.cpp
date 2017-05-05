/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurface.h"

#include "../2d/2D.h"
#include "gfxPrefs.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "MozFramebuffer.h"
#include "nsThreadUtils.h"
#include "ScopedGLHelpers.h"
#include "SharedSurfaceEGL.h"
#include "SharedSurfaceGL.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/Unused.h"

#ifdef GL_PROVIDER_GLX
#include "SharedSurfaceGLX.h"
#endif
#ifdef XP_MACOSX
#include "SharedSurfaceIO.h"
#endif
#ifdef XP_WIN
#include "SharedSurfaceANGLE.h"
#include "SharedSurfaceD3D11Interop.h"
#endif

namespace mozilla {
namespace gl {

SharedSurface::SharedSurface(const SharedSurfaceType type, GLContext* const gl,
                             const gfx::IntSize& size, const bool canRecycle,
                             UniquePtr<MozFramebuffer> mozFB)
    : mType(type)
    , mGL(gl)
    , mSize(size)
    , mCanRecycle(canRecycle)

    , mMozFB(Move(mozFB))
    , mFB(mMozFB ? mMozFB->mFB : 0)

    , mIsLocked(false)
    , mIsWriteAcquired(false)
    , mIsReadAcquired(false)
#ifdef DEBUG
    , mOwningThread(NS_GetCurrentThread())
#endif
{ }

SharedSurface::~SharedSurface()
{
    MOZ_ASSERT(!mIsLocked);
    MOZ_ASSERT(!mIsWriteAcquired);
    MOZ_ASSERT(!mIsReadAcquired);
}

layers::TextureFlags
SharedSurface::GetTextureFlags() const
{
    return layers::TextureFlags::NO_FLAGS;
}

void
SharedSurface::CopyFrom(const MozFramebuffer* const src)
{
    MOZ_ASSERT(!mIsLocked);
    MOZ_ASSERT(mIsWriteAcquired);
    MOZ_RELEASE_ASSERT(mSize == src->mSize);

    const auto colorTex = src->ColorTex();
    MOZ_RELEASE_ASSERT(colorTex);

    mGL->PushSurfaceLock(this);

    if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
        mGL->BlitHelper()->BlitFramebufferToFramebuffer(src->mFB, mFB, mSize, mSize);
    } else {
        mGL->BlitHelper()->DrawBlitTextureToFramebuffer(colorTex, mFB, mSize, mSize,
                                                        src->mColorTarget);
    }
    mGL->PopSurfaceLock();
}

void
SharedSurface::CopyFrom(SharedSurface* const src)
{
    MOZ_RELEASE_ASSERT(src->mSize == mSize);
    MOZ_ASSERT(!src->mIsLocked);
    MOZ_ASSERT(!mIsLocked);
    MOZ_ASSERT(src->mIsReadAcquired);
    MOZ_ASSERT(mIsWriteAcquired);

    if (src->mType == mType) {
        if (CopyFromSameType(src))
            return;
    } else {
        MOZ_RELEASE_ASSERT(src->mType == SharedSurfaceType::Basic);
    }

    CopyFrom(mMozFB.get());
}

////////////////////////////////////////////////////////////////////////////////
// SurfaceFactory

/*static*/ UniquePtr<SurfaceFactory>
SurfaceFactory::Create(GLContext* const gl, const bool depthStencil,
                       layers::KnowsCompositor* const compositor,
                       const layers::TextureFlags flags)
{
    return Create(gl, depthStencil, compositor->GetTextureForwarder(),
                  compositor->GetCompositorBackendType(), flags);
}

/*static*/ UniquePtr<SurfaceFactory>
SurfaceFactory::Create(GLContext* const gl, const bool depthStencil,
                       layers::LayersIPCChannel* const ipcChannel,
                       const layers::LayersBackend backend,
                       const layers::TextureFlags flags)
{
    UniquePtr<SurfaceFactory> factory;
    if (!gfxPrefs::WebGLForceLayersReadback()) {
        switch (backend) {
            case mozilla::layers::LayersBackend::LAYERS_OPENGL:
#if defined(XP_MACOSX)
                factory = AsUnique(new SurfaceFactory_IOSurface(gl, depthStencil,
                                                                ipcChannel, flags));
#elif defined(GL_PROVIDER_GLX)
                factory = SurfaceFactory_GLXDrawable::Create(gl, depthStencil, ipcChannel,
                                                             flags);
#elif defined(MOZ_WIDGET_UIKIT)
                factory = MakeUnique<SurfaceFactory_GLTexture>(gl, depthStencil,
                                                               ipcChannel, flags);
#else
                if (gl->GetContextType() == GLContextType::EGL) {
                    if (XRE_IsParentProcess()) {
                        factory = SurfaceFactory_EGLImage::Create(gl, depthStencil,
                                                                  ipcChannel, flags);
                    }
                }
#endif
                break;

            case mozilla::layers::LayersBackend::LAYERS_D3D11:
#ifdef XP_WIN
                factory = SurfaceFactory_ANGLEShareHandle::Create(gl, depthStencil,
                                                                  ipcChannel, flags);

                if (!factory) {
                  factory = SurfaceFactory_D3D11Interop::Create(gl, depthStencil,
                                                                ipcChannel, flags);
                }
#endif
                break;

            default:
#ifdef GL_PROVIDER_GLX
                factory = SurfaceFactory_GLXDrawable::Create(gl, depthStencil, ipcChannel,
                                                             flags);
#endif
                break;
        }
    }

    return factory;
}

////////////////////////////////////////

SurfaceFactory::SurfaceFactory(const SharedSurfaceType type, GLContext* const gl,
                               const bool depthStencil,
                               layers::LayersIPCChannel* const allocator,
                               const layers::TextureFlags flags)
    : mType(type)
    , mGL(gl)
    , mDepthStencil(depthStencil)
    , mAllocator(allocator)
    , mFlags(flags)

    , mDepthStencilSize(0, 0)
    , mDepthRB(0)
    , mStencilRB(0)
    , mMutex("SurfaceFactory::mMutex")
{ }

SurfaceFactory::~SurfaceFactory()
{
    if (mGL->MakeCurrent()) {
        DeleteDepthStencil();
    }

    while (!mRecycleTotalPool.empty()) {
        RefPtr<layers::SharedSurfaceTextureClient> tex = *mRecycleTotalPool.begin();
        StopRecycling(tex);
        tex->CancelWaitForRecycle();
    }

    MOZ_RELEASE_ASSERT(mRecycleTotalPool.empty(),"GFX: Surface recycle pool not empty.");

    // If we mRecycleFreePool.clear() before StopRecycling(), we may try to recycle it,
    // fail, call StopRecycling(), then return here and call it again.
    mRecycleFreePool.clear();
}

void
SurfaceFactory::DeleteDepthStencil()
{
    mDepthStencilSize = gfx::IntSize(0, 0);

    if (mDepthRB || mStencilRB) {
        if (mDepthRB == mStencilRB) {
            mGL->fDeleteRenderbuffers(1, &mDepthRB);
        } else {
            mGL->fDeleteRenderbuffers(1, &mDepthRB);
            mGL->fDeleteRenderbuffers(1, &mStencilRB);
        }
        mStencilRB = 0;
        mDepthRB = 0;
    }
}

UniquePtr<SharedSurface>
SurfaceFactory::NewSharedSurface(const gfx::IntSize& size)
{
    UniquePtr<SharedSurface> surf = NewSharedSurfaceImpl(size);
    if (!surf)
        return nullptr;

    if (!surf->mFB)
        return Move(surf);

    if (size != mDepthStencilSize) {
        DeleteDepthStencil();
        mDepthStencilSize = size;

        const auto fnCreateRB = [&](GLenum format) {
            MOZ_ASSERT(format);
            GLuint rb = 0;
            mGL->fGenRenderbuffers(1, &rb);
            const ScopedBindRenderbuffer bindRB(mGL, rb);
            mGL->fRenderbufferStorage(LOCAL_GL_RENDERBUFFER, format,
                                      mDepthStencilSize.width, mDepthStencilSize.height);
            return rb;
        };

        GLContext::LocalErrorScope errScope(*mGL);

        if (mDepthStencil) {
            if (mGL->IsSupported(GLFeature::packed_depth_stencil)) {
                mDepthRB = fnCreateRB(LOCAL_GL_DEPTH24_STENCIL8);
                mStencilRB = mDepthRB;
            } else {
                mDepthRB = fnCreateRB(LOCAL_GL_DEPTH_COMPONENT24);
                mStencilRB = fnCreateRB(LOCAL_GL_STENCIL_INDEX8);
            }
        }

        const auto err = errScope.GetError();
        if (err) {
            MOZ_RELEASE_ASSERT(err == LOCAL_GL_OUT_OF_MEMORY);
            DeleteDepthStencil();
            return nullptr;
        }
    }

    const ScopedBindFramebuffer bindFB(mGL, surf->mFB);
    if (mDepthRB) {
        mGL->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_DEPTH_ATTACHMENT,
                                      LOCAL_GL_RENDERBUFFER, mDepthRB);
    }
    if (mStencilRB) {
        mGL->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_STENCIL_ATTACHMENT,
                                      LOCAL_GL_RENDERBUFFER, mStencilRB);
    }

    const auto status = mGL->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
    if (status != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
        MOZ_RELEASE_ASSERT(false);
        return nullptr;
    }

    return Move(surf);
}

RefPtr<layers::SharedSurfaceTextureClient>
SurfaceFactory::NewTexClient(const gfx::IntSize& size)
{
    while (!mRecycleFreePool.empty()) {
        RefPtr<layers::SharedSurfaceTextureClient> cur = mRecycleFreePool.front();
        mRecycleFreePool.pop();

        if (cur->Surf()->mSize == size) {
            cur->Surf()->WaitForBufferOwnership();
            return cur.forget();
        }

        StopRecycling(cur);
    }

    UniquePtr<SharedSurface> surf = NewSharedSurface(size);
    if (!surf)
        return nullptr;

    RefPtr<layers::SharedSurfaceTextureClient> ret;
    ret = layers::SharedSurfaceTextureClient::Create(Move(surf), this, mAllocator,
                                                     mFlags);
    StartRecycling(ret);
    return ret;
}

RefPtr<layers::SharedSurfaceTextureClient>
SurfaceFactory::CloneTexClient(SharedSurface* const src)
{
    const auto destClient = NewTexClient(src->mSize);
    if (!destClient)
        return nullptr;

    const auto& dest = destClient->Surf();

    src->ProducerReadAcquire();
    dest->ProducerAcquire();

    dest->CopyFrom(src);

    dest->ProducerRelease();
    src->ProducerReadRelease();

    return destClient;
}

void
SurfaceFactory::StartRecycling(layers::SharedSurfaceTextureClient* tc)
{
    tc->SetRecycleCallback(&SurfaceFactory::RecycleCallback, static_cast<void*>(this));

    bool didInsert = mRecycleTotalPool.insert(tc);
    MOZ_RELEASE_ASSERT(didInsert, "GFX: Shared surface texture client was not inserted to recycle.");
    mozilla::Unused << didInsert;
}

void
SurfaceFactory::StopRecycling(layers::SharedSurfaceTextureClient* tc)
{
    MutexAutoLock autoLock(mMutex);
    // Must clear before releasing ref.
    tc->ClearRecycleCallback();

    bool didErase = mRecycleTotalPool.erase(tc);
    MOZ_RELEASE_ASSERT(didErase, "GFX: Shared texture surface client was not erased.");
    mozilla::Unused << didErase;
}

/*static*/ void
SurfaceFactory::RecycleCallback(layers::TextureClient* rawTC, void* rawFactory)
{
    RefPtr<layers::SharedSurfaceTextureClient> tc;
    tc = static_cast<layers::SharedSurfaceTextureClient*>(rawTC);
    SurfaceFactory* factory = static_cast<SurfaceFactory*>(rawFactory);

    if (tc->Surf()->mCanRecycle) {
        if (factory->Recycle(tc))
            return;
    }

    // Did not recover the tex client. End the (re)cycle!
    factory->StopRecycling(tc);
}

bool
SurfaceFactory::Recycle(layers::SharedSurfaceTextureClient* texClient)
{
    MOZ_ASSERT(texClient);
    MutexAutoLock autoLock(mMutex);

    if (mRecycleFreePool.size() >= 2) {
        return false;
    }

    RefPtr<layers::SharedSurfaceTextureClient> texClientRef = texClient;
    mRecycleFreePool.push(texClientRef);
    return true;
}

////////////////////////////////////////////////////////////////////////////////

bool
MorphableSurfaceFactory::Morph(layers::KnowsCompositor* const info, const bool force)
{
    if (mFactory->mType != SharedSurfaceType::Basic && !force)
        return false;

    auto newFactory = SurfaceFactory::Create(mFactory->mGL, mFactory->mDepthStencil, info,
                                             mFactory->mFlags);
    if (!newFactory)
        return false;

    mFactory = Move(newFactory);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// ScopedReadbackFB

ScopedReadbackFB::ScopedReadbackFB(SharedSurface* src)
    : mGL(src->mGL)
    , mAutoFB(mGL)
{
    mGL->PushSurfaceLock(src);

    if (src->NeedsIndirectReads()) {
        mIndirectFB = MozFramebuffer::Create(mGL, src->mSize, 0, false);
        MOZ_RELEASE_ASSERT(mIndirectFB);

        {
            MOZ_ASSERT(mIndirectFB->ColorTex());
            const ScopedBindTexture autoTex(mGL, mIndirectFB->ColorTex());
            mGL->fCopyTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, 0, 0,
                                 mIndirectFB->mSize.width, mIndirectFB->mSize.height, 0);
        }

        mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mIndirectFB->mFB);
    } else {
        mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, src->mFB);
    }
}

ScopedReadbackFB::~ScopedReadbackFB()
{
    mGL->PopSurfaceLock();
}

////////////////////////////////////////////////////////////////////////////////

class AutoLockBits
{
    gfx::DrawTarget* mDT;
    uint8_t* mLockedBits;

public:
    explicit AutoLockBits(gfx::DrawTarget* dt)
        : mDT(dt)
        , mLockedBits(nullptr)
    {
        MOZ_ASSERT(mDT);
    }

    bool Lock(uint8_t** data, gfx::IntSize* size, int32_t* stride,
              gfx::SurfaceFormat* format)
    {
        if (!mDT->LockBits(data, size, stride, format))
            return false;

        mLockedBits = *data;
        return true;
    }

    ~AutoLockBits() {
        if (mLockedBits)
            mDT->ReleaseBits(mLockedBits);
    }
};

bool
ReadbackSharedSurface(SharedSurface* src, gfx::DrawTarget* dst)
{
    AutoLockBits lock(dst);

    uint8_t* dstBytes;
    gfx::IntSize dstSize;
    int32_t dstStride;
    gfx::SurfaceFormat dstFormat;
    if (!lock.Lock(&dstBytes, &dstSize, &dstStride, &dstFormat))
        return false;

    const bool isDstRGBA = (dstFormat == gfx::SurfaceFormat::R8G8B8A8 ||
                            dstFormat == gfx::SurfaceFormat::R8G8B8X8);
    MOZ_ASSERT_IF(!isDstRGBA, dstFormat == gfx::SurfaceFormat::B8G8R8A8 ||
                              dstFormat == gfx::SurfaceFormat::B8G8R8X8);

    size_t width = src->mSize.width;
    size_t height = src->mSize.height;
    MOZ_ASSERT(width == (size_t)dstSize.width);
    MOZ_ASSERT(height == (size_t)dstSize.height);

    GLenum readGLFormat;
    GLenum readType;

    {
        ScopedReadbackFB autoReadback(src);

        // We have a source FB, now we need a format.
        GLenum dstGLFormat = isDstRGBA ? LOCAL_GL_BGRA : LOCAL_GL_RGBA;
        GLenum dstType = LOCAL_GL_UNSIGNED_BYTE;

        // We actually don't care if they match, since we can handle
        // any read{Format,Type} we get.
        GLContext* gl = src->mGL;
        GetActualReadFormats(gl, dstGLFormat, dstType, &readGLFormat,
                             &readType);

        MOZ_ASSERT(readGLFormat == LOCAL_GL_RGBA ||
                   readGLFormat == LOCAL_GL_BGRA);
        MOZ_ASSERT(readType == LOCAL_GL_UNSIGNED_BYTE);

        // ReadPixels from the current FB into lockedBits.
        {
            size_t alignment = 8;
            if (dstStride % 4 == 0)
                alignment = 4;

            ScopedPackState scopedPackState(gl);
            if (alignment != 4) {
                gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, alignment);
            }

            gl->raw_fReadPixels(0, 0, width, height, readGLFormat, readType,
                                dstBytes);
        }
    }

    const bool isReadRGBA = readGLFormat == LOCAL_GL_RGBA;

    if (isReadRGBA != isDstRGBA) {
        for (size_t j = 0; j < height; ++j) {
            uint8_t* rowItr = dstBytes + j*dstStride;
            uint8_t* rowEnd = rowItr + 4*width;
            while (rowItr != rowEnd) {
                Swap(rowItr[0], rowItr[2]);
                rowItr += 4;
            }
        }
    }

    return true;
}

void
Readback(SharedSurface* const src, gfx::DataSourceSurface* const dest)
{
    MOZ_ASSERT(src && dest);
    MOZ_ASSERT(dest->GetSize() == src->mSize);

    GLContext* const gl = src->mGL;
    gl->MakeCurrent();

    gl->PushSurfaceLock(src);

    {
        const ScopedReadbackFB autoReadback(src);

        // We're consuming from the producer side, so which do we use?
        // Really, we just want a read-only lock, so ConsumerAcquire is the best match.
        src->ProducerReadAcquire();

        ReadPixelsIntoDataSurface(gl, dest);

        src->ProducerReadRelease();
    }
}

uint32_t
ReadPixel(SharedSurface* src)
{
    GLContext* gl = src->mGL;

    uint32_t pixel;

    ScopedReadbackFB a(src);
    {
        ScopedPackState scopedPackState(gl);

        UniquePtr<uint8_t[]> bytes(new uint8_t[4]);
        gl->raw_fReadPixels(0, 0, 1, 1, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE,
                            bytes.get());
        memcpy(&pixel, bytes.get(), 4);
    }

    return pixel;
}

} // namespace gl

} /* namespace mozilla */
