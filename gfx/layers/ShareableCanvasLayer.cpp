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
  : CopyableCanvasLayer(aLayerManager, aImplData)
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

bool
ShareableCanvasLayer::UpdateTarget(DrawTarget* aDestTarget)
{
  MOZ_ASSERT(aDestTarget);
  if (!aDestTarget) {
    return false;
  }

  const auto& frontTex = GetFrontTex();
  if (!frontTex) {
    MOZ_ASSERT(mBufferProvider);
    if (!mBufferProvider) {
      return false;
    }
    bool success = false;
    RefPtr<SourceSurface> surface = mBufferProvider->BorrowSnapshot();
    MOZ_ASSERT(surface);
    if (surface) {
      aDestTarget->CopySurface(surface,
                               IntRect(0, 0, mBounds.width, mBounds.height),
                               IntPoint(0, 0));
      success = true;
    }
    mBufferProvider->ReturnSnapshot(surface.forget());
    return success;
  }
  const auto& frontSurf = frontTex->Surf();

  const auto& readSize = frontSurf->mSize;
  const SurfaceFormat format = SurfaceFormat::B8G8R8A8;
  const bool isNonPremult = bool(frontTex->GetFlags() & TextureFlags::NON_PREMULTIPLIED);

  // Try to read back directly into aDestTarget's output buffer
  uint8_t* destData;
  IntSize destSize;
  int32_t destStride;
  SurfaceFormat destFormat;
  if (aDestTarget->LockBits(&destData, &destSize, &destStride, &destFormat)) {
    if (destSize == readSize && destFormat == format) {
      RefPtr<DataSourceSurface> data =
        Factory::CreateWrappingDataSourceSurface(destData, destStride, destSize, destFormat);
      Readback(frontSurf, data);
      if (isNonPremult) {
        gfxUtils::PremultiplyDataSurface(data, data);
      }
      aDestTarget->ReleaseBits(destData);
      return true;
    }
    aDestTarget->ReleaseBits(destData);
  }

  RefPtr<DataSourceSurface> resultSurf = GetTempSurface(readSize, format);
  // There will already be a warning from inside of GetTempSurface, but
  // it doesn't hurt to complain:
  if (NS_WARN_IF(!resultSurf)) {
    return false;
  }

  // Readback handles Flush/MarkDirty.
  Readback(frontSurf, resultSurf);
  if (isNonPremult) {
    gfxUtils::PremultiplyDataSurface(resultSurf, resultSurf);
  }

  aDestTarget->CopySurface(resultSurf,
                           IntRect(0, 0, readSize.width, readSize.height),
                           IntPoint(0, 0));

  return true;
}

CanvasClient::CanvasClientType
ShareableCanvasLayer::GetCanvasClientType()
{
  if (mAsyncRenderer) {
    return CanvasClient::CanvasClientAsync;
  }

  if (mWebGL || mCanvas2D) {
    return CanvasClient::CanvasClientTypeShSurf;
  }
  return CanvasClient::CanvasClientSurface;
}

void
ShareableCanvasLayer::UpdateCompositableClient()
{
  if (!mCanvasClient) {
    mCanvasClient = CanvasClient::CreateCanvasClient(GetCanvasClientType(),
                                                     GetForwarder(),
                                                     TextureFlags::DEFAULT);
    if (!mCanvasClient) {
      return;
    }

    AttachCompositable();
  }

  if (mCanvasClient && mAsyncRenderer) {
    mCanvasClient->UpdateAsync(mAsyncRenderer);
  }

  if (!IsDirty()) {
    return;
  }
  Painted();

  FirePreTransactionCallback();
  if (mBufferProvider && mBufferProvider->GetTextureClient()) {
    if (!mBufferProvider->SetForwarder(mManager->AsShadowForwarder())) {
      gfxCriticalNote << "BufferProvider::SetForwarder failed";
      return;
    }
    mCanvasClient->UpdateFromTexture(mBufferProvider->GetTextureClient());
  } else {
    mCanvasClient->Update(gfx::IntSize(mBounds.width, mBounds.height), this);
  }

  FireDidTransactionCallback();

  mCanvasClient->Updated();
}

} // namespace layers
} // namespace mozilla
