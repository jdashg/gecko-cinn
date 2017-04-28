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

CanvasLayer::Data::Data(const gfx::IntSize& size, const bool isAlphaPremult)
  : mSize(size)
  , mIsAlphaPremult(isAlphaPremult)
  , mBufferProvider(nullptr)
{ }
CanvasLayer::Data::~Data() = default;


CopyableCanvasLayer::CopyableCanvasLayer(LayerManager* aLayerManager, void *aImplData) :
  CanvasLayer(aLayerManager, aImplData)
  , mIsAlphaPremultiplied(true)
  , mOriginPos(gl::OriginPos::TopLeft)
{
  MOZ_COUNT_CTOR(CopyableCanvasLayer);
}

CopyableCanvasLayer::~CopyableCanvasLayer()
{
  MOZ_COUNT_DTOR(CopyableCanvasLayer);
}

void
CopyableCanvasLayer::Initialize(const Data& aData)
{
  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);
  mIsAlphaPremultiplied = aData.mIsAlphaPremult;

  mBufferProvider = aData.mBufferProvider;
  mWebGL = aData.mWebGL;
  mCanvas2D = aData.mCanvas2D;

  if (bool(mBufferProvider) + bool(mWebGL) + bool(mCanvas2D) != 1) {
    MOZ_CRASH("GFX: must have exactly one CanvasLayer source");
  }
}

bool
CopyableCanvasLayer::IsDataValid(const Data& aData)
{
  return aData.mSize.width == mBounds.width &&
         aData.mSize.height == mBounds.height &&
         aData.mIsAlphaPremult == mIsAlphaPremultiplied &&
         aData.mBufferProvider == mBufferProvider;
         aData.mWebGL == mWebGL;
         aData.mCanvas2D == mCanvas2D;
}

DataSourceSurface*
CopyableCanvasLayer::GetTempSurface(const IntSize& aSize,
                                    const SurfaceFormat aFormat)
{
  if (!mCachedTempSurface ||
      aSize != mCachedTempSurface->GetSize() ||
      aFormat != mCachedTempSurface->GetFormat())
  {
    // Create a surface aligned to 8 bytes since that's the highest alignment WebGL can handle.
    uint32_t stride = GetAlignedStride<8>(aSize.width, BytesPerPixel(aFormat));
    mCachedTempSurface = Factory::CreateDataSourceSurfaceWithStride(aSize, aFormat, stride);
  }

  return mCachedTempSurface;
}

RefPtr<layers::SharedSurfaceTextureClient>
CopyableCanvasLayer::GetFrontTex() const
{
  RefPtr<SharedSurfaceTextureClient> frontTex;
  if (mWebGL) {
    frontTex = mWebGL->FrontBuffer();
  } else if (mCanvas2D) {
    frontTex = mCanvas2D->GetFrontBuffer();
  } else {
    // Don't warn.
    return nullptr;
  }

  if (!frontTex) {
    NS_WARNING("Null frame received.");
    return nullptr;
  }
  return frontTex;
}

} // namespace layers
} // namespace mozilla
