/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MOZILLA_GFX_DRAWTARGETWEBGL_H
#define _MOZILLA_GFX_DRAWTARGETWEBGL_H

#include "mozilla/gfx/2D.h"
#include <sstream>

namespace mozilla {

class WebGLContext;

namespace gfx {

class DataSourceSurface;
class DrawTargetSkia;
class SourceSurfaceSkia;

class DrawTargetWebgl : public DrawTarget {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawTargetWebgl, override)

 private:
  IntSize mSize;
  RefPtr<WebGLContext> mWebgl;
  RefPtr<DrawTargetSkia> mSkia;
  RefPtr<DataSourceSurface> mSnapshot;

 public:
  // D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION is 16k, and this is pretty common
  // for ES3.
  static constexpr size_t kMaxSurfaceSize = 16384;
  static size_t GetMaxSurfaceSize() {
    return kMaxSurfaceSize;
  }

  DrawTargetWebgl();
  ~DrawTargetWebgl();

  bool Init(const IntSize& aSize, SurfaceFormat aFormat);

  DrawTargetType GetType() const override {
    return DrawTargetType::HARDWARE_RASTER;
  }
  BackendType GetBackendType() const override {
    return BackendType::WEBGL;
  }
  IntSize GetSize() const override { return mSize; };

  // -
  // TODO: Implement (copied from DrawTargetSkia)

  already_AddRefed<SourceSurface> Snapshot() override;
  already_AddRefed<SourceSurface> GetBackingSurface() override;
  void DetachAllSnapshots() override;

  bool LockBits(uint8_t** aData, IntSize* aSize, int32_t* aStride,
                        SurfaceFormat* aFormat,
                        IntPoint* aOrigin = nullptr) override;
  void ReleaseBits(uint8_t* aData) override;
  void Flush() override;
  void DrawSurface(
      SourceSurface* aSurface, const Rect& aDest, const Rect& aSource,
      const DrawSurfaceOptions& aSurfOptions = DrawSurfaceOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;
  void DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                          const Point& aDestPoint,
                          const DrawOptions& aOptions = DrawOptions()) override;
  void DrawSurfaceWithShadow(SourceSurface* aSurface,
                                     const Point& aDest,
                                     const DeviceColor& aColor,
                                     const Point& aOffset, Float aSigma,
                                     CompositionOp aOperator) override;
  void ClearRect(const Rect& aRect) override;
  void CopySurface(SourceSurface* aSurface, const IntRect& aSourceRect,
                           const IntPoint& aDestination) override;
  void FillRect(const Rect& aRect, const Pattern& aPattern,
                        const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeRect(const Rect& aRect, const Pattern& aPattern,
                          const StrokeOptions& aStrokeOptions = StrokeOptions(),
                          const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeLine(const Point& aStart, const Point& aEnd,
                          const Pattern& aPattern,
                          const StrokeOptions& aStrokeOptions = StrokeOptions(),
                          const DrawOptions& aOptions = DrawOptions()) override;
  void Stroke(const Path* aPath, const Pattern& aPattern,
                      const StrokeOptions& aStrokeOptions = StrokeOptions(),
                      const DrawOptions& aOptions = DrawOptions()) override;
  void Fill(const Path* aPath, const Pattern& aPattern,
                    const DrawOptions& aOptions = DrawOptions()) override;

  void FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                          const Pattern& aPattern,
                          const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeGlyphs(
      ScaledFont* aFont, const GlyphBuffer& aBuffer, const Pattern& aPattern,
      const StrokeOptions& aStrokeOptions = StrokeOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;
  void Mask(const Pattern& aSource, const Pattern& aMask,
                    const DrawOptions& aOptions = DrawOptions()) override;
  void MaskSurface(
      const Pattern& aSource, SourceSurface* aMask, Point aOffset,
      const DrawOptions& aOptions = DrawOptions()) override;
  bool Draw3DTransformedSurface(SourceSurface* aSurface,
                                        const Matrix4x4& aMatrix) override;
  void PushClip(const Path* aPath) override;
  void PushClipRect(const Rect& aRect) override;
  void PushDeviceSpaceClipRects(const IntRect* aRects,
                                        uint32_t aCount) override;
  void PopClip() override;
  void PushLayer(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                         const Matrix& aMaskTransform,
                         const IntRect& aBounds = IntRect(),
                         bool aCopyBackground = false) override;
  void PushLayerWithBlend(
      bool aOpaque, Float aOpacity, SourceSurface* aMask,
      const Matrix& aMaskTransform, const IntRect& aBounds = IntRect(),
      bool aCopyBackground = false,
      CompositionOp aCompositionOp = CompositionOp::OP_OVER) override;
  void PopLayer() override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromData(
      unsigned char* aData, const IntSize& aSize, int32_t aStride,
      SurfaceFormat aFormat) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurface(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurfaceForUnknownAlpha(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromNativeSurface(
      const NativeSurface& aSurface) const override;
  already_AddRefed<DrawTarget> CreateSimilarDrawTarget(
      const IntSize& aSize, SurfaceFormat aFormat) const override;
  bool CanCreateSimilarDrawTarget(const IntSize& aSize,
                                          SurfaceFormat aFormat) const override;
  RefPtr<DrawTarget> CreateClippedDrawTarget(
      const Rect& aBounds, SurfaceFormat aFormat) override;

  already_AddRefed<PathBuilder> CreatePathBuilder(
      FillRule aFillRule = FillRule::FILL_WINDING) const override;
  already_AddRefed<GradientStops> CreateGradientStops(
      GradientStop* aStops, uint32_t aNumStops,
      ExtendMode aExtendMode = ExtendMode::CLAMP) const override;
  already_AddRefed<FilterNode> CreateFilter(FilterType aType) override;
  void SetTransform(const Matrix& aTransform) override;
  void* GetNativeSurface(NativeSurfaceType aType) override;

  operator std::string() const {
    std::stringstream stream;
    stream << "DrawTargetWebgl(" << this << ")";
    return stream.str();
  }
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _MOZILLA_GFX_DRAWTARGETWEBGL_H
