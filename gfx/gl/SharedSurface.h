/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* SharedSurface abstracts an actual surface (can be a GL texture, but
 * not necessarily) that handles sharing.
 * Its specializations are:
 *     SharedSurface_Basic (client-side bitmap, does readback)
 *     SharedSurface_GLTexture
 *     SharedSurface_EGLImage
 *     SharedSurface_ANGLEShareHandle
 */

#ifndef SHARED_SURFACE_H_
#define SHARED_SURFACE_H_

#include <queue>
#include <set>
#include <stdint.h>

#include "GLContextTypes.h"
#include "GLDefs.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "ScopedGLHelpers.h"
#include "SurfaceTypes.h"

class nsIThread;

namespace mozilla {
namespace gfx {
class DataSourceSurface;
class DrawTarget;
} // namespace gfx

namespace layers {
class LayersIPCChannel;
class SharedSurfaceTextureClient;
enum class TextureFlags : uint32_t;
class SurfaceDescriptor;
class TextureClient;
} // namespace layers

namespace gl {

class GLContext;
class SurfaceFactory;
class ShSurfHandle;

class SharedSurface
{
public:
    const SharedSurfaceType mType;

    const WeakPtr<GLContext> mGL;
    const GLenum mTexTarget;
    const GLuint mTex;
    const bool mOwnsTex;
    const GLuint mFB;

    const gfx::IntSize mSize;
    const bool mHasAlpha;
    const bool mCanRecycle;
protected:
    bool mIsLocked;
    bool mIsWriteAcquired;
    bool mIsReadAcquired;
#ifdef DEBUG
    nsIThread* const mOwningThread;
#endif

    static GLuint CreateFB(GLContext* gl);

    SharedSurface(SharedSurfaceType type,
                  GLContext* gl,
                  GLenum texTarget, GLuint tex, bool ownsTex,
                  GLuint fbIfNoTex,
                  const gfx::IntSize& size,
                  bool hasAlpha,
                  bool canRecycle);

public:
    virtual ~SharedSurface();

    virtual void CopyFrom(SharedSurface* src);
    virtual bool CopyFromSameType(SharedSurface* src) { return false; }

    // Specifies to the TextureClient any flags which
    // are required by the SharedSurface backend.
    virtual layers::TextureFlags GetTextureFlags() const;

    bool IsLocked() const { return mIsLocked; }
    //bool IsProducerAcquired() const { return mIsProducerAcquired; }

protected:
    // This locks the SharedSurface as the production buffer for the context.
    // This is needed by backends which use PBuffers and/or EGLSurfaces.
    void LockProd() {
        MOZ_ASSERT(!mIsLocked);
        mIsLocked = true;
        LockProdImpl();
    }
    void UnlockProd() {
        MOZ_ASSERT(mIsLocked);
        mIsLocked = false;
        UnlockProdImpl();
    }
    friend class GLContext;

    virtual void LockProdImpl() = 0;
    virtual void UnlockProdImpl() = 0;

    virtual void ProducerAcquireImpl() = 0;
    virtual void ProducerReleaseImpl() = 0;
    virtual void ProducerReadAcquireImpl() { ProducerAcquireImpl(); }
    virtual void ProducerReadReleaseImpl() { ProducerReleaseImpl(); }

public:
    void ProducerAcquire() {
        MOZ_ASSERT(!mIsWriteAcquired);
        MOZ_ASSERT(!mIsReadAcquired);
        ProducerAcquireImpl();
        mIsWriteAcquired = true;
    }
    void ProducerRelease() {
        MOZ_ASSERT(mIsWriteAcquired);
        MOZ_ASSERT(!mIsReadAcquired);
        ProducerReleaseImpl();
        mIsWriteAcquired = false;
    }
    void ProducerReadAcquire() {
        MOZ_ASSERT(!mIsWriteAcquired);
        MOZ_ASSERT(!mIsReadAcquired);
        ProducerReadAcquireImpl();
        mIsReadAcquired = true;
    }
    void ProducerReadRelease() {
        MOZ_ASSERT(!mIsWriteAcquired);
        MOZ_ASSERT(mIsReadAcquired);
        ProducerReadReleaseImpl();
        mIsReadAcquired = false;
    }

    // This function waits until the buffer is no longer being used.
    // To optimize the performance, some implementaions recycle SharedSurfaces
    // even when its buffer is still being used.
    virtual void WaitForBufferOwnership() {}

    virtual bool CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x,
                                GLint y, GLsizei width, GLsizei height, GLint border)
    {
        return false;
    }

    virtual bool ReadPixels(GLint x, GLint y,
                            GLsizei width, GLsizei height,
                            GLenum format, GLenum type,
                            GLvoid* pixels)
    {
        return false;
    }

    virtual bool NeedsIndirectReads() const {
        return false;
    }

    virtual bool ToSurfaceDescriptor(layers::SurfaceDescriptor* const out_descriptor) = 0;

    virtual bool ReadbackBySharedHandle(gfx::DataSourceSurface* out_surface) {
        return false;
    }
};

template<typename T>
class RefSet
{
    std::set<T*> mSet;

public:
    ~RefSet() {
        clear();
    }

    auto begin() -> decltype(mSet.begin()) {
        return mSet.begin();
    }

    void clear() {
        for (auto itr = mSet.begin(); itr != mSet.end(); ++itr) {
            (*itr)->Release();
        }
        mSet.clear();
    }

    bool empty() const {
        return mSet.empty();
    }

    bool insert(T* x) {
        if (mSet.insert(x).second) {
            x->AddRef();
            return true;
        }

        return false;
    }

    bool erase(T* x) {
        if (mSet.erase(x)) {
            x->Release();
            return true;
        }

        return false;
    }
};

template<typename T>
class RefQueue
{
    std::queue<T*> mQueue;

public:
    ~RefQueue() {
        clear();
    }

    void clear() {
        while (!empty()) {
            pop();
        }
    }

    bool empty() const {
        return mQueue.empty();
    }

    size_t size() const {
        return mQueue.size();
    }

    void push(T* x) {
        mQueue.push(x);
        x->AddRef();
    }

    T* front() const {
        return mQueue.front();
    }

    void pop() {
        T* x = mQueue.front();
        x->Release();
        mQueue.pop();
    }
};

class SurfaceFactory : public SupportsWeakPtr<SurfaceFactory>
{
public:
    // Should use the VIRTUAL version, but it's currently incompatible
    // with SupportsWeakPtr. (bug 1049278)
    MOZ_DECLARE_WEAKREFERENCE_TYPENAME(SurfaceFactory)

    const SharedSurfaceType mType;
    GLContext* const mGL;
    const SurfaceCaps mCaps;
    const RefPtr<layers::LayersIPCChannel> mAllocator;
    const layers::TextureFlags mFlags;
    const GLFormats mFormats;
    gfx::IntSize mDepthStencilSize;
    GLuint mDepthRB;
    GLuint mStencilRB;
    Mutex mMutex;
protected:
    RefQueue<layers::SharedSurfaceTextureClient> mRecycleFreePool;
    RefSet<layers::SharedSurfaceTextureClient> mRecycleTotalPool;

    SurfaceFactory(SharedSurfaceType type, GLContext* gl, const SurfaceCaps& caps,
                   const RefPtr<layers::LayersIPCChannel>& allocator,
                   const layers::TextureFlags& flags);

public:
    virtual ~SurfaceFactory();

private:
    void DeleteDepthStencil();

protected:
    virtual UniquePtr<SharedSurface> NewSharedSurfaceImpl(const gfx::IntSize& size) = 0;

    void StartRecycling(layers::SharedSurfaceTextureClient* tc);
    void SetRecycleCallback(layers::SharedSurfaceTextureClient* tc);
    void StopRecycling(layers::SharedSurfaceTextureClient* tc);

public:
    UniquePtr<SharedSurface> NewSharedSurface(const gfx::IntSize& size);

    RefPtr<layers::SharedSurfaceTextureClient>
    NewTexClient(const gfx::IntSize& size);

    RefPtr<layers::SharedSurfaceTextureClient>
    CloneTexClient(SharedSurface* src);

    static void RecycleCallback(layers::TextureClient* tc, void* /*closure*/);

    // Auto-deletes surfs of the wrong type.
    bool Recycle(layers::SharedSurfaceTextureClient* texClient);
};

class ScopedReadbackFB
{
    GLContext* const mGL;
    ScopedBindFramebuffer mAutoFB;
    ScopedBypassScreen mBypass;
    GLuint mTempFB;
    GLuint mTempTex;

public:
    explicit ScopedReadbackFB(SharedSurface* src);
    ~ScopedReadbackFB();
};

bool ReadbackSharedSurface(SharedSurface* src, gfx::DrawTarget* dst);
uint32_t ReadPixel(SharedSurface* src);

} // namespace gl
} // namespace mozilla

#endif // SHARED_SURFACE_H_
