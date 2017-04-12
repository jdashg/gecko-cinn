/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ShareableCanvasLayer.h"

#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/layers/AsyncCanvasRenderer.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "SharedSurfaceGL.h"            // for SurfaceFactory_GLTexture, etc
#include "../../dom/canvas/WebGLContext.h"

namespace mozilla {
namespace layers {

ShareableCanvasLayer::ShareableCanvasLayer(LayerManager* aLayerManager, void *aImplData)
  : ContentCanvasLayer(aLayerManager, aImplData)
  , mFlags(TextureFlags::NO_FLAGS)
{
  MOZ_COUNT_CTOR(ShareableCanvasLayer);
}

ShareableCanvasLayer::~ShareableCanvasLayer()
{
  MOZ_COUNT_DTOR(ShareableCanvasLayer);
  if (mBufferProvider) {
    mBufferProvider->ClearCachedResources();
  }
  if (mCanvasClient) {
    mCanvasClient->OnDetach();
    mCanvasClient = nullptr;
  }
}

void
ShareableCanvasLayer::Initialize(const Data& aData)
{
  CopyableCanvasLayer::Initialize(aData);

  mCanvasClient = nullptr;

  const auto& forwarder = GetForwarder();
  if (mWebGL) {
    mWebGL->mSurfFactory.Morph(forwarder);
  } else if (mCanvas2D) {
    mCanvas2D->mSurfFactory.Morph(forwarder);
    mCanvas2D->mProvider->SetForwarder(forwarder);
  }
}

void
ShareableCanvasLayer::UpdateCompositableClient()
{
  if (!mCanvasClient) {
    mCanvasClient = new CanvasClient(GetForwarder(), TextureFlags::DEFAULT);
    AttachCompositable();
  }

  const auto& frame = GetFrameForRedraw();
  if (!frame)
    return;

  RefPtr<TextureClient> texClient = frame->mTexClient;
  if (!texClient) {
    texClient = frame->mProvider->GetTextureClient();
  }

  if (texClient && mCanvasClient->UseTexClient(texClient)) {
    mMappableA = nullptr;
    mMappableB = nullptr;
    return;
  }

  UniquePtr<TextureClientAutoLock> mapped;
  const auto fnLock = [&](TextureClient* texClient) {
    if (!texClient)
      return false;

    mapped.reset(new TextureClientAutoLock(texClient, OpenMode::OPEN_WRITE_ONLY));
    if (!autoLock.Succeeded()) {
      mapped = nullptr;
      return false;
    }

    return true;
  };

  do {
    if (fnLock(mLockableA)) {
      texClient = mLockableA;
      break;
    }

    if (fnLock(mLockableB)) {
      texClient = mLockableB;
      break;
    }

    mLockableB = mLockableA;
    mLockableA = CreateTextureClientForCanvas();

    if (fnLock(mLockableA)) {
      texClient = mLockableA;
      break;
    }

    gfxCriticalNote << "Failed to lock a texClient.";
    return;
  } while (false);

  RefPtr<DrawTarget> target = mCopiedBackBuffer->BorrowDrawTarget();
  if (target) {
    aLayer->DrawTo(target);
    updated = true;
  }

  const auto& sourceSurface = ToSourceSurface()
  const auto compatibleTexClient = [&]() {
    if (frame->mTexClient)
      return frame->mTexClient;

    if (frame->mProvider->GetTextureClient() &&
        frame->mProvider->SetForwarder(mManager->AsShadowForwarder()))
    {
      return frame->mProvider->GetTextureClient();
    }
  };

  if (frame->mTexClient) {
    mCanvasClient->SetTexClient(frame->mTexClient);
  } else if (frame->mProvider->GetTextureClient()) {
    if (!frame->mProvider->SetForwarder(mManager->AsShadowForwarder())) {
      gfxCriticalNote << "BufferProvider::SetForwarder failed";
      return;
    }
    mCanvasClient->SetTexClient(frame->mProvider->GetTextureClient());
  } else {
    mCanvasClient->CopyFrameFromLayer(this, frame);
  }

  mCanvasClient->Updated();
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

} // namespace layers
} // namespace mozilla
