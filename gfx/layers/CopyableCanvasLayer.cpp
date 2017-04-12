/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CopyableCanvasLayer.h"

#include "BasicLayersImpl.h"            // for FillWithMask, etc
#include "GLContext.h"                  // for GLContext
#include "SharedSurface.h"              // for SharedSurface
#include "SharedSurfaceGL.h"              // for SharedSurface
#include "gfxPattern.h"                 // for gfxPattern, etc
#include "gfxPlatform.h"                // for gfxPlatform, gfxImageFormat
#include "gfxRect.h"                    // for gfxRect
#include "gfxUtils.h"                   // for gfxUtils
#include "gfx2DGlue.h"                  // for thebes --> moz2d transition
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/gfx/BaseSize.h"       // for BaseSize
#include "mozilla/gfx/Tools.h"
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/layers/AsyncCanvasRenderer.h"
#include "mozilla/layers/PersistentBufferProvider.h"
#include "nsDebug.h"                    // for NS_ASSERTION, NS_WARNING, etc
#include "nsISupportsImpl.h"            // for gfxContext::AddRef, etc
#include "nsRect.h"                     // for mozilla::gfx::IntRect
#include "gfxUtils.h"
#include "client/TextureClientSharedSurface.h"
#include "../../dom/canvas/WebGLContext.h"

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;
using namespace mozilla::gl;

CanvasLayer::FrameData::FrameData(layers::SharedSurfaceTextureClient* const texClient)
  : mTexClient(texClient)
{ }

CanvasLayer::FrameData::FrameData(PersistentBufferProvider* const provider)
  : mProvider(provider)
  , mBorrowedSnapshot(mProvider->BorrowSnapshot())
{ }

CanvasLayer::FrameData::~FrameData()
{
  if (mProvider) {
    mProvider->ReturnSnapshot(mBorrowedSnapshot);
  }
}

// -------

ContentCanvasLayer::ContentCanvasLayer(LayerManager* aLayerManager, void* aImplData) :
  CanvasLayer(aLayerManager, aImplData)
{
  MOZ_COUNT_CTOR(ContentCanvasLayer);
}

ContentCanvasLayer::~ContentCanvasLayer()
{
  MOZ_COUNT_DTOR(ContentCanvasLayer);
}

void
ContentCanvasLayer::Initialize(const Data& aData)
{
  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);

  mWebGL = aData.mWebGL;
  mCanvas2D = aData.mCanvas2D;
  mSource = (mWebGL ? mWebGL.get() : mCanvas2D.get());

  if (bool(mWebGL) + bool(mCanvas2D) != 1) {
    MOZ_CRASH("GFX: must have exactly one CanvasLayer source");
  }
}

// -------

RefPtr<DataSourceSurface>
ContentCanvasLayer::GetReusableSurface(const IntSize& aSize,
                                       const SurfaceFormat aFormat) const
{
  if (!mReusableSurface ||
      aSize != mReusableSurface->GetSize() ||
      aFormat != mReusableSurface->GetFormat())
  {
    // Create a surface aligned to 8 bytes since that's the highest alignment WebGL can handle.
    uint32_t stride = GetAlignedStride<8>(aSize.width, BytesPerPixel(aFormat));
    mReusableSurface = Factory::CreateDataSourceSurfaceWithStride(aSize, aFormat, stride);
  }

  return mReusableSurface;
}

RefPtr<SourceSurface>
ContentCanvasLayer::ToSourceSurface(TextureClient* const texClient,
                                    gl::OriginPos* const out_origin) const
{
  const auto& sharedSurf = texClient->Surf();

  const IntSize readSize(sharedSurf->mSize);
  const bool isOpaque = bool(GetContentFlags() & CONTENT_OPAQUE);
  const SurfaceFormat format = (isOpaque ? SurfaceFormat::B8G8R8X8
                                         : SurfaceFormat::B8G8R8A8);
  const RefPtr<DataSourceSurface> sourceSurf = GetReusableSurface(readSize, format);
  // There will already be a warning from inside of GetTempSurface, but
  // it doesn't hurt to complain:
  if (NS_WARN_IF(!sourceSurf)) {
    return nullptr;
  }

  Readback(sharedSurf, sourceSurf);

  const bool isNonPremult = bool(texClient->GetFlags() & TextureFlags::NON_PREMULTIPLIED);
  if (isNonPremult && !isOpaque) {
    gfxUtils::PremultiplyDataSurface(sourceSurf, sourceSurf);
  }

  if (texClient->GetFlags() & TextureFlags::ORIGIN_BOTTOM_LEFT) {
    *out_origin = gl::OriginPos::BottomLeft;
  } else {
    *out_origin = gl::OriginPos::TopLeft;
  }
  return sourceSurf;
}

// -------

RefPtr<CanvasLayer::FrameData>
ContentCanvasLayer::GetFrameForRedraw()
{
  if (IsHidden())
    return nullptr;

  auto* pDestFrameID = &mFrameID;
  if (ShouldAlwaysRedraw())
    pDestFrameID = nullptr;
  }
  return mSource->GetNextFrame(pDestFrameID);
}

// -------

void
ContentCanvasLayer::DrawTo(const FrameData* const frame, gfx::DrawTarget* const aDT,
                           const gfx::Point& aDeviceOffset, Layer* const aMaskLayer) const
{
  RefPtr<SourceSurface> surf;
  auto surfOrigin = gl::OriginPos::TopLeft;
  PersistentBufferProvider* usedProvider = nullptr;
  if (frame->mTexClient) {
    surf = ToSourceSurface(frame->mTexClient, &surfOrigin);
  } else {
    usedProvider = frame->mProvider;
    surf = usedProvider->BorrowSnapshot();
  }
  if (!surf)
    return false;

  const bool needsYFlip = (surfOrigin != gl::OriginPos::TopLeft);

  Matrix oldTM;
  if (needsYFlip) {
    oldTM = aDT->GetTransform();
    aDT->SetTransform(Matrix(oldTM).
                        PreTranslate(0.0f, mBounds.height).
                        PreScale(1.0f, -1.0f));
  }

  FillRectWithMask(aDT, aDeviceOffset,
                   Rect(0, 0, mBounds.width, mBounds.height),
                   surf, mSamplingFilter,
                   DrawOptions(GetEffectiveOpacity(), GetEffectiveOperator(this)),
                   aMaskLayer);

  if (needsYFlip) {
    aDT->SetTransform(oldTM);
  }

  if (usedProvider) {
    usedProvider->ReturnSnapshot(surf);
  }
  return true;
}

} // namespace layers
} // namespace mozilla
