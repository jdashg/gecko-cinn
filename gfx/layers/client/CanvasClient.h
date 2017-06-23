/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_CANVASCLIENT_H
#define MOZILLA_GFX_CANVASCLIENT_H

#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for RefPtr, already_AddRefed
#include "mozilla/layers/CompositableClient.h"  // for CompositableClient
#include "mozilla/layers/CompositorTypes.h"  // for TextureInfo, etc
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor
#include "mozilla/layers/TextureClient.h"  // for TextureClient, etc
#include "mozilla/layers/PersistentBufferProvider.h"

#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/gfx/Types.h"          // for SurfaceFormat

namespace mozilla {
namespace layers {

class CompositableForwarder;
class ShadowableLayer;
class SharedSurfaceTextureClient;

/**
 * Compositable client for 2d and webgl canvas.
 */
class CanvasClient final : public CompositableClient
{
public:
  CanvasClient(CompositableForwarder* aFwd, TextureFlags aFlags)
    : CompositableClient(aFwd, aFlags)
    , mFrameID(0)
  {
    mTextureFlags = aFlags;
  }

  virtual bool AddTextureClient(TextureClient* aTexture) override
  {
    ++mFrameID;
    return CompositableClient::AddTextureClient(aTexture);
  }

  virtual TextureInfo GetTextureInfo() const override
  {
    return TextureInfo(CompositableType::IMAGE, mTextureFlags);
  }

  bool UseTexClient(TextureClient* aTexture);

protected:
  int32_t mFrameID;
  RefPtr<TextureClient> mTexClient;
};

} // namespace layers
} // namespace mozilla

#endif
