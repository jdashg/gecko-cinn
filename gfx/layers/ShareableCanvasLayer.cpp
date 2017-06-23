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

  const auto texClient = [&]() {
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

} // namespace layers
} // namespace mozilla
