/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasClient.h"

#include "ClientCanvasLayer.h"          // for ClientCanvasLayer
#include "GLContext.h"                  // for GLContext
#include "ScopedGLHelpers.h"
#include "SharedSurface.h"
#include "../../gl/ScopedReadbackFB.h"
#include "gfx2DGlue.h"                  // for ImageFormatToSurfaceFormat
#include "gfxPlatform.h"                // for gfxPlatform
#include "GLReadTexImageHelper.h"
#include "mozilla/gfx/BaseSize.h"       // for BaseSize
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/AsyncCanvasRenderer.h"
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/CompositorBridgeChild.h" // for CompositorBridgeChild
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/TextureClient.h"  // for TextureClient, etc
#include "mozilla/layers/TextureClientOGL.h"
#include "nsDebug.h"                    // for printf_stderr, NS_ASSERTION
#include "nsXULAppAPI.h"                // for XRE_GetProcessType, etc
#include "ShareableCanvasLayer.h"
#include "TextureClientSharedSurface.h"

using namespace mozilla::gfx;
using namespace mozilla::gl;

namespace mozilla {
namespace layers {

////////////////////////////////////////

bool
CanvasClient::UseTexClient(TextureClient* const aTexture)
{
  MOZ_ASSERT(aTexture);

  if (!aTexture->IsSharedWithCompositor()) {
    if (!AddTextureClient(aTexture)) {
      MOZ_ASSERT(false, "AddTextureClient failed");
      return;
    }
  }

  mTexClient = aTexture;

  AutoTArray<CompositableForwarder::TimedTextureClient,1> textures;
  CompositableForwarder::TimedTextureClient* t = textures.AppendElement();
  t->mTextureClient = mTexClient;
  t->mPictureRect = nsIntRect(nsIntPoint(0, 0), aTexture->GetSize());
  t->mFrameID = mFrameID;

  const auto& forwarder = GetForwarder();
  forwarder->UseTextures(this, textures);
  mTexClient->SyncWithObject(forwarder->GetSyncObject());
}

static bool
DrawToTexClient(const SourceSurface* const src, TextureClient* const dest)
{
  TextureClientAutoLock autoLock(mCopiedBackBuffer, OpenMode::OPEN_WRITE_ONLY);
  if (!autoLock.Succeeded())
    return false;

  const RefPtr<DrawTarget> target = mCopiedBackBuffer->BorrowDrawTarget();
  if (target) {
    aLayer->DrawTo(target);
    updated = true;
  }

}
/*
void
CanvasClientSync::CopyFromLayer(gfx::IntSize aSize, ShareableCanvasLayer* aLayer)
{
  mSetTexClient = nullptr;

  //AutoRemoveTexture autoRemove(this);
  if (mCopiedBackBuffer && (mCopiedBackBuffer->IsReadLocked() ||
                            mCopiedBackBuffer->GetSize() != aSize))
  {
    //autoRemove.mTexture = mBackBuffer;
    mCopiedBackBuffer = nullptr;
  }

  bool bufferCreated = false;
  if (!mCopiedBackBuffer) {
    bool isOpaque = (aLayer->GetContentFlags() & Layer::CONTENT_OPAQUE);
    gfxContentType contentType = isOpaque
                                                ? gfxContentType::COLOR
                                                : gfxContentType::COLOR_ALPHA;
    gfx::SurfaceFormat surfaceFormat
      = gfxPlatform::GetPlatform()->Optimal2DFormatForContent(contentType);
    TextureFlags flags = TextureFlags::DEFAULT;
    if (mTextureFlags & TextureFlags::ORIGIN_BOTTOM_LEFT) {
      flags |= TextureFlags::ORIGIN_BOTTOM_LEFT;
    }

    mCopiedBackBuffer = CreateTextureClientForCanvas(surfaceFormat, aSize, flags, aLayer);
    if (!mCopiedBackBuffer) {
      NS_WARNING("Failed to allocate the TextureClient");
      return;
    }
    mCopiedBackBuffer->EnableReadLock();
    MOZ_ASSERT(mCopiedBackBuffer->CanExposeDrawTarget());

    bufferCreated = true;
  }

  bool updated = false;
  {
    TextureClientAutoLock autoLock(mCopiedBackBuffer, OpenMode::OPEN_WRITE_ONLY);
    if (!autoLock.Succeeded()) {
      mCopiedBackBuffer = nullptr;
      return;
    }

    RefPtr<DrawTarget> target = mCopiedBackBuffer->BorrowDrawTarget();
    if (target) {
      aLayer->DrawTo(target);
      updated = true;
    }
  }

  if (bufferCreated && !AddTextureClient(mCopiedBackBuffer)) {
    mCopiedBackBuffer = nullptr;
    return;
  }

  mCopiedFrontBuffer.swap(mCopiedBackBuffer);

  if (updated) {
    UseAndSyncTexture(mCopiedFrontBuffer);
  }
}

already_AddRefed<TextureClient>
CanvasClientSync::CreateTextureClientForCanvas(gfx::SurfaceFormat aFormat,
                                               gfx::IntSize aSize,
                                               TextureFlags aFlags,
                                               ShareableCanvasLayer* aLayer)
{
  if (aLayer->IsGLLayer()) {
    // We want a cairo backend here as we don't want to be copying into
    // an accelerated backend and we like LockBits to work. This is currently
    // the most effective way to make this work.
    return TextureClient::CreateForRawBufferAccess(GetForwarder(),
                                                   aFormat, aSize, BackendType::CAIRO,
                                                   mTextureFlags | aFlags);
  }

#ifdef XP_WIN
  return CreateTextureClientForDrawing(aFormat, aSize, BackendSelector::Canvas, aFlags);
#else
  // XXX - We should use CreateTextureClientForDrawing, but we first need
  // to use double buffering.
  gfx::BackendType backend = gfxPlatform::GetPlatform()->GetPreferredCanvasBackend();
  return TextureClient::CreateForRawBufferAccess(GetForwarder(),
                                                 aFormat, aSize, backend,
                                                 mTextureFlags | aFlags);
#endif
}
*/
} // namespace layers
} // namespace mozilla
