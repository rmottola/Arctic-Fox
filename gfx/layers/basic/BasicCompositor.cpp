/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicCompositor.h"
#include "BasicLayersImpl.h"            // for FillRectWithMask
#include "TextureHostBasic.h"
#include "mozilla/layers/Effects.h"
#include "nsIWidget.h"
#include "gfx2DGlue.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/gfx/Tools.h"
#include "gfxUtils.h"
#include "YCbCrUtils.h"
#include <algorithm>
#include "ImageContainer.h"
#include "gfxPrefs.h"
#ifdef MOZ_ENABLE_SKIA
#include "skia/include/core/SkCanvas.h"              // for SkCanvas
#include "skia/include/core/SkBitmapDevice.h"        // for SkBitmapDevice
#else
#define PIXMAN_DONT_DEFINE_STDINT
#include "pixman.h"                     // for pixman_f_transform, etc
#endif

namespace mozilla {
using namespace mozilla::gfx;

namespace layers {

class DataTextureSourceBasic : public DataTextureSource
                             , public TextureSourceBasic
{
public:
  virtual const char* Name() const override { return "DataTextureSourceBasic"; }

  explicit DataTextureSourceBasic(DataSourceSurface* aSurface)
  : mSurface(aSurface)
  , mWrappingExistingData(!!aSurface)
  {}

  virtual DataTextureSource* AsDataTextureSource() override
  {
    // If the texture wraps someone else's memory we'd rather not use it as
    // a DataTextureSource per say (that is call Update on it).
    return mWrappingExistingData ? nullptr : this;
  }

  virtual TextureSourceBasic* AsSourceBasic() override { return this; }

  virtual gfx::SourceSurface* GetSurface(DrawTarget* aTarget) override { return mSurface; }

  SurfaceFormat GetFormat() const override
  {
    return mSurface ? mSurface->GetFormat() : gfx::SurfaceFormat::UNKNOWN;
  }

  virtual IntSize GetSize() const override
  {
    return mSurface ? mSurface->GetSize() : gfx::IntSize(0, 0);
  }

  virtual bool Update(gfx::DataSourceSurface* aSurface,
                      nsIntRegion* aDestRegion = nullptr,
                      gfx::IntPoint* aSrcOffset = nullptr) override
  {
    MOZ_ASSERT(!mWrappingExistingData);
    if (mWrappingExistingData) {
      return false;
    }
    mSurface = aSurface;
    return true;
  }

  virtual void DeallocateDeviceData() override
  {
    mSurface = nullptr;
    SetUpdateSerial(0);
  }

public:
  RefPtr<gfx::DataSourceSurface> mSurface;
  bool mWrappingExistingData;
};

BasicCompositor::BasicCompositor(nsIWidget *aWidget)
  : mWidget(aWidget)
  , mDidExternalComposition(false)
{
  MOZ_COUNT_CTOR(BasicCompositor);

  mMaxTextureSize =
    Factory::GetMaxSurfaceSize(gfxPlatform::GetPlatform()->GetContentBackendFor(LayersBackend::LAYERS_BASIC));
}

BasicCompositor::~BasicCompositor()
{
  MOZ_COUNT_DTOR(BasicCompositor);
}

bool
BasicCompositor::Initialize()
{
  return mWidget ? mWidget->InitCompositor(this) : false;
};

int32_t
BasicCompositor::GetMaxTextureSize() const
{
  return mMaxTextureSize;
}

void
BasicCompositingRenderTarget::BindRenderTarget()
{
  if (mClearOnBind) {
    mDrawTarget->ClearRect(Rect(0, 0, mSize.width, mSize.height));
    mClearOnBind = false;
  }
}

void BasicCompositor::Destroy()
{
  mWidget->CleanupRemoteDrawing();
  mWidget = nullptr;
}

TextureFactoryIdentifier
BasicCompositor::GetTextureFactoryIdentifier()
{
  TextureFactoryIdentifier ident(LayersBackend::LAYERS_BASIC,
                                 XRE_GetProcessType(),
                                 GetMaxTextureSize());
  return ident;
}

already_AddRefed<CompositingRenderTarget>
BasicCompositor::CreateRenderTarget(const IntRect& aRect, SurfaceInitMode aInit)
{
  MOZ_ASSERT(aRect.width != 0 && aRect.height != 0, "Trying to create a render target of invalid size");

  if (aRect.width * aRect.height == 0) {
    return nullptr;
  }

  RefPtr<DrawTarget> target = mDrawTarget->CreateSimilarDrawTarget(aRect.Size(), SurfaceFormat::B8G8R8A8);

  if (!target) {
    return nullptr;
  }

  RefPtr<BasicCompositingRenderTarget> rt = new BasicCompositingRenderTarget(target, aRect);

  return rt.forget();
}

already_AddRefed<CompositingRenderTarget>
BasicCompositor::CreateRenderTargetFromSource(const IntRect &aRect,
                                              const CompositingRenderTarget *aSource,
                                              const IntPoint &aSourcePoint)
{
  MOZ_CRASH("GFX: Shouldn't be called!");
  return nullptr;
}

already_AddRefed<CompositingRenderTarget>
BasicCompositor::CreateRenderTargetForWindow(const LayoutDeviceIntRect& aRect, SurfaceInitMode aInit, BufferMode aBufferMode)
{
  MOZ_ASSERT(mDrawTarget);
  MOZ_ASSERT(aRect.width != 0 && aRect.height != 0, "Trying to create a render target of invalid size");

  if (aRect.width * aRect.height == 0) {
    return nullptr;
  }

  RefPtr<BasicCompositingRenderTarget> rt;
  IntRect rect = aRect.ToUnknownRect();

  if (aBufferMode != BufferMode::BUFFER_NONE) {
    RefPtr<DrawTarget> target = mWidget->CreateBackBufferDrawTarget(mDrawTarget, aRect);
    if (!target) {
      return nullptr;
    }
    rt = new BasicCompositingRenderTarget(target, rect);
  } else {
    IntRect windowRect = rect;
    // Adjust bounds rect to account for new origin at (0, 0).
    if (windowRect.Size() != mDrawTarget->GetSize()) {
      windowRect.ExpandToEnclose(IntPoint(0, 0));
    }
    rt = new BasicCompositingRenderTarget(mDrawTarget, windowRect);
    if (aInit == INIT_MODE_CLEAR) {
      mDrawTarget->ClearRect(Rect(rect - rt->GetOrigin()));
    }
  }

  return rt.forget();
}

already_AddRefed<DataTextureSource>
BasicCompositor::CreateDataTextureSource(TextureFlags aFlags)
{
  RefPtr<DataTextureSource> result = new DataTextureSourceBasic(nullptr);
  return result.forget();
}

already_AddRefed<DataTextureSource>
BasicCompositor::CreateDataTextureSourceAround(DataSourceSurface* aSurface)
{
  RefPtr<DataTextureSource> result = new DataTextureSourceBasic(aSurface);
  return result.forget();
}

bool
BasicCompositor::SupportsEffect(EffectTypes aEffect)
{
  return aEffect != EffectTypes::YCBCR && aEffect != EffectTypes::COMPONENT_ALPHA;
}

static void
DrawSurfaceWithTextureCoords(DrawTarget *aDest,
                             const gfx::Rect& aDestRect,
                             SourceSurface *aSource,
                             const gfx::Rect& aTextureCoords,
                             gfx::Filter aFilter,
                             const DrawOptions& aOptions,
                             SourceSurface *aMask,
                             const Matrix* aMaskTransform)
{
  if (!aSource) {
    gfxWarning() << "DrawSurfaceWithTextureCoords problem " << gfx::hexa(aSource) << " and " << gfx::hexa(aMask);
    return;
  }

  // Convert aTextureCoords into aSource's coordinate space
  gfxRect sourceRect(aTextureCoords.x * aSource->GetSize().width,
                     aTextureCoords.y * aSource->GetSize().height,
                     aTextureCoords.width * aSource->GetSize().width,
                     aTextureCoords.height * aSource->GetSize().height);

  // Floating point error can accumulate above and we know our visible region
  // is integer-aligned, so round it out.
  sourceRect.Round();

  // Compute a transform that maps sourceRect to aDestRect.
  Matrix matrix =
    gfxUtils::TransformRectToRect(sourceRect,
                                  gfx::IntPoint(aDestRect.x, aDestRect.y),
                                  gfx::IntPoint(aDestRect.XMost(), aDestRect.y),
                                  gfx::IntPoint(aDestRect.XMost(), aDestRect.YMost()));

  // Only use REPEAT if aTextureCoords is outside (0, 0, 1, 1).
  gfx::Rect unitRect(0, 0, 1, 1);
  ExtendMode mode = unitRect.Contains(aTextureCoords) ? ExtendMode::CLAMP : ExtendMode::REPEAT;

  FillRectWithMask(aDest, aDestRect, aSource, aFilter, aOptions,
                   mode, aMask, aMaskTransform, &matrix);
}

#ifdef MOZ_ENABLE_SKIA
static SkMatrix
Matrix3DToSkia(const Matrix4x4& aMatrix)
{
  SkMatrix transform;
  transform.setAll(aMatrix._11,
                   aMatrix._21,
                   aMatrix._41,
                   aMatrix._12,
                   aMatrix._22,
                   aMatrix._42,
                   aMatrix._14,
                   aMatrix._24,
                   aMatrix._44);

  return transform;
}

static void
Transform(DataSourceSurface* aDest,
          DataSourceSurface* aSource,
          const Matrix4x4& aTransform,
          const Point& aDestOffset)
{
  if (aTransform.IsSingular()) {
    return;
  }

  IntSize destSize = aDest->GetSize();
  SkImageInfo destInfo = SkImageInfo::Make(destSize.width,
                                           destSize.height,
                                           kBGRA_8888_SkColorType,
                                           kPremul_SkAlphaType);
  SkBitmap destBitmap;
  destBitmap.setInfo(destInfo, aDest->Stride());
  destBitmap.setPixels((uint32_t*)aDest->GetData());
  SkCanvas destCanvas(destBitmap);

  IntSize srcSize = aSource->GetSize();
  SkImageInfo srcInfo = SkImageInfo::Make(srcSize.width,
                                          srcSize.height,
                                          kBGRA_8888_SkColorType,
                                          kPremul_SkAlphaType);
  SkBitmap src;
  src.setInfo(srcInfo, aSource->Stride());
  src.setPixels((uint32_t*)aSource->GetData());

  Matrix4x4 transform = aTransform;
  transform.PostTranslate(Point3D(-aDestOffset.x, -aDestOffset.y, 0));
  destCanvas.setMatrix(Matrix3DToSkia(transform));

  SkPaint paint;
  paint.setXfermodeMode(SkXfermode::kSrc_Mode);
  paint.setAntiAlias(true);
  paint.setFilterQuality(kLow_SkFilterQuality);
  SkRect destRect = SkRect::MakeXYWH(0, 0, srcSize.width, srcSize.height);
  destCanvas.drawBitmapRect(src, destRect, &paint);
}
#else
static pixman_transform
Matrix3DToPixman(const Matrix4x4& aMatrix)
{
  pixman_f_transform transform;

  transform.m[0][0] = aMatrix._11;
  transform.m[0][1] = aMatrix._21;
  transform.m[0][2] = aMatrix._41;
  transform.m[1][0] = aMatrix._12;
  transform.m[1][1] = aMatrix._22;
  transform.m[1][2] = aMatrix._42;
  transform.m[2][0] = aMatrix._14;
  transform.m[2][1] = aMatrix._24;
  transform.m[2][2] = aMatrix._44;

  pixman_transform result;
  pixman_transform_from_pixman_f_transform(&result, &transform);

  return result;
}

static void
Transform(DataSourceSurface* aDest,
          DataSourceSurface* aSource,
          const Matrix4x4& aTransform,
          const Point& aDestOffset)
{
  IntSize destSize = aDest->GetSize();
  pixman_image_t* dest = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                  destSize.width,
                                                  destSize.height,
                                                  (uint32_t*)aDest->GetData(),
                                                  aDest->Stride());

  IntSize srcSize = aSource->GetSize();
  pixman_image_t* src = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                 srcSize.width,
                                                 srcSize.height,
                                                 (uint32_t*)aSource->GetData(),
                                                 aSource->Stride());

  MOZ_ASSERT(src !=0 && dest != 0, "Failed to create pixman images?");

  pixman_transform pixTransform = Matrix3DToPixman(aTransform);
  pixman_transform pixTransformInverted;

  // If the transform is singular then nothing would be drawn anyway, return here
  if (!pixman_transform_invert(&pixTransformInverted, &pixTransform)) {
    pixman_image_unref(dest);
    pixman_image_unref(src);
    return;
  }
  pixman_image_set_transform(src, &pixTransformInverted);

  pixman_image_composite32(PIXMAN_OP_SRC,
                           src,
                           nullptr,
                           dest,
                           aDestOffset.x,
                           aDestOffset.y,
                           0,
                           0,
                           0,
                           0,
                           destSize.width,
                           destSize.height);

  pixman_image_unref(dest);
  pixman_image_unref(src);
}
#endif

static inline IntRect
RoundOut(Rect r)
{
  r.RoundOut();
  return IntRect(r.x, r.y, r.width, r.height);
}

static void
SetupMask(const EffectChain& aEffectChain,
          DrawTarget* aDest,
          const IntPoint& aOffset,
          RefPtr<SourceSurface>& aMaskSurface,
          Matrix& aMaskTransform)
{
  if (aEffectChain.mSecondaryEffects[EffectTypes::MASK]) {
    EffectMask *effectMask = static_cast<EffectMask*>(aEffectChain.mSecondaryEffects[EffectTypes::MASK].get());
    aMaskSurface = effectMask->mMaskTexture->AsSourceBasic()->GetSurface(aDest);
    if (!aMaskSurface) {
      gfxWarning() << "Invalid sourceMask effect";
    }
    MOZ_ASSERT(effectMask->mMaskTransform.Is2D(), "How did we end up with a 3D transform here?!");
    aMaskTransform = effectMask->mMaskTransform.As2D();
    aMaskTransform.PostTranslate(-aOffset.x, -aOffset.y);
  }
}

void
BasicCompositor::DrawQuad(const gfx::Rect& aRect,
                          const gfx::Rect& aClipRect,
                          const EffectChain &aEffectChain,
                          gfx::Float aOpacity,
                          const gfx::Matrix4x4& aTransform,
                          const gfx::Rect& aVisibleRect)
{
  RefPtr<DrawTarget> buffer = mRenderTarget->mDrawTarget;

  // For 2D drawing, |dest| and |buffer| are the same surface. For 3D drawing,
  // |dest| is a temporary surface.
  RefPtr<DrawTarget> dest = buffer;

  AutoRestoreTransform autoRestoreTransform(dest);

  Matrix newTransform;
  Rect transformBounds;
  Matrix4x4 new3DTransform;
  IntPoint offset = mRenderTarget->GetOrigin();

  if (aTransform.Is2D()) {
    newTransform = aTransform.As2D();
  } else {
    // Create a temporary surface for the transform.
    dest = gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(RoundOut(aRect).Size(), SurfaceFormat::B8G8R8A8);
    if (!dest) {
      return;
    }

    dest->SetTransform(Matrix::Translation(-aRect.x, -aRect.y));

    // Get the bounds post-transform.
    transformBounds = aTransform.TransformAndClipBounds(aRect, Rect(offset.x, offset.y, buffer->GetSize().width, buffer->GetSize().height));
    transformBounds.RoundOut();

    if (transformBounds.IsEmpty()) {
      return;
    }

    // Propagate the coordinate offset to our 2D draw target.
    newTransform = Matrix::Translation(transformBounds.x, transformBounds.y);

    // When we apply the 3D transformation, we do it against a temporary
    // surface, so undo the coordinate offset.
    new3DTransform = aTransform;
    new3DTransform.PreTranslate(aRect.x, aRect.y, 0);
  }

  buffer->PushClipRect(aClipRect);

  newTransform.PostTranslate(-offset.x, -offset.y);
  buffer->SetTransform(newTransform);

  RefPtr<SourceSurface> sourceMask;
  Matrix maskTransform;
  if (aTransform.Is2D()) {
    SetupMask(aEffectChain, dest, offset, sourceMask, maskTransform);
  }

  CompositionOp blendMode = CompositionOp::OP_OVER;
  if (Effect* effect = aEffectChain.mSecondaryEffects[EffectTypes::BLEND_MODE].get()) {
    blendMode = static_cast<EffectBlendMode*>(effect)->mBlendMode;
  }

  switch (aEffectChain.mPrimaryEffect->mType) {
    case EffectTypes::SOLID_COLOR: {
      EffectSolidColor* effectSolidColor =
        static_cast<EffectSolidColor*>(aEffectChain.mPrimaryEffect.get());

      bool unboundedOp = !IsOperatorBoundByMask(blendMode);
      if (unboundedOp) {
        dest->PushClipRect(aRect);
      }

      FillRectWithMask(dest, aRect, effectSolidColor->mColor,
                       DrawOptions(aOpacity, blendMode), sourceMask, &maskTransform);

      if (unboundedOp) {
        dest->PopClip();
      }
      break;
    }
    case EffectTypes::RGB: {
      TexturedEffect* texturedEffect =
          static_cast<TexturedEffect*>(aEffectChain.mPrimaryEffect.get());
      TextureSourceBasic* source = texturedEffect->mTexture->AsSourceBasic();

      if (source && texturedEffect->mPremultiplied) {
          DrawSurfaceWithTextureCoords(dest, aRect,
                                       source->GetSurface(dest),
                                       texturedEffect->mTextureCoords,
                                       texturedEffect->mFilter,
                                       DrawOptions(aOpacity, blendMode),
                                       sourceMask, &maskTransform);
      } else if (source) {
        SourceSurface* srcSurf = source->GetSurface(dest);
        if (srcSurf) {
          RefPtr<DataSourceSurface> srcData = srcSurf->GetDataSurface();

          // Yes, we re-create the premultiplied data every time.
          // This might be better with a cache, eventually.
          RefPtr<DataSourceSurface> premultData = gfxUtils::CreatePremultipliedDataSurface(srcData);

          DrawSurfaceWithTextureCoords(dest, aRect,
                                       premultData,
                                       texturedEffect->mTextureCoords,
                                       texturedEffect->mFilter,
                                       DrawOptions(aOpacity, blendMode),
                                       sourceMask, &maskTransform);
        }
      } else {
        gfxDevCrash(LogReason::IncompatibleBasicTexturedEffect) << "Bad for basic with " << texturedEffect->mTexture->Name() << " and " << gfx::hexa(sourceMask);
      }

      break;
    }
    case EffectTypes::YCBCR: {
      NS_RUNTIMEABORT("Can't (easily) support component alpha with BasicCompositor!");
      break;
    }
    case EffectTypes::RENDER_TARGET: {
      EffectRenderTarget* effectRenderTarget =
        static_cast<EffectRenderTarget*>(aEffectChain.mPrimaryEffect.get());
      RefPtr<BasicCompositingRenderTarget> surface
        = static_cast<BasicCompositingRenderTarget*>(effectRenderTarget->mRenderTarget.get());
      RefPtr<SourceSurface> sourceSurf = surface->mDrawTarget->Snapshot();

      DrawSurfaceWithTextureCoords(dest, aRect,
                                   sourceSurf,
                                   effectRenderTarget->mTextureCoords,
                                   effectRenderTarget->mFilter,
                                   DrawOptions(aOpacity, blendMode),
                                   sourceMask, &maskTransform);
      break;
    }
    case EffectTypes::COMPONENT_ALPHA: {
      NS_RUNTIMEABORT("Can't (easily) support component alpha with BasicCompositor!");
      break;
    }
    default: {
      NS_RUNTIMEABORT("Invalid effect type!");
      break;
    }
  }

  if (!aTransform.Is2D()) {
    dest->Flush();

    RefPtr<SourceSurface> snapshot = dest->Snapshot();
    RefPtr<DataSourceSurface> source = snapshot->GetDataSurface();
    RefPtr<DataSourceSurface> temp =
      Factory::CreateDataSourceSurface(RoundOut(transformBounds).Size(), SurfaceFormat::B8G8R8A8
#ifdef MOZ_ENABLE_SKIA
        , true
#endif
        );
    if (NS_WARN_IF(!temp)) {
      buffer->PopClip();
      return;
    }

    Transform(temp, source, new3DTransform, transformBounds.TopLeft());

    SetupMask(aEffectChain, buffer, offset, sourceMask, maskTransform);

    // Adjust for the fact that our content now start at 0,0 instead
    // of the top left of transformBounds.
    transformBounds.MoveTo(0, 0);
    maskTransform.PostTranslate(-transformBounds.x, -transformBounds.y);

    if (sourceMask) {
      // Transform the source by it's normal transform, and then the inverse
      // of the mask transform so that it's in the mask's untransformed
      // coordinate space.
      Matrix old = buffer->GetTransform();
      Matrix sourceTransform = old;

      Matrix inverseMask = maskTransform;
      inverseMask.Invert();

      sourceTransform *= inverseMask;

      SurfacePattern source(temp, ExtendMode::CLAMP, sourceTransform);

      buffer->PushClipRect(transformBounds);

      // Mask in the untransformed coordinate space, and then transform
      // by the mask transform to put the result back into destination
      // coords.
      buffer->SetTransform(maskTransform);
      buffer->MaskSurface(source, sourceMask, Point(0, 0));
      buffer->SetTransform(old);

      buffer->PopClip();
    } else {
      buffer->DrawSurface(temp, transformBounds, transformBounds);
    }
  }

  buffer->PopClip();
}

void
BasicCompositor::ClearRect(const gfx::Rect& aRect)
{
  mRenderTarget->mDrawTarget->ClearRect(aRect);
}

void
BasicCompositor::BeginFrame(const nsIntRegion& aInvalidRegion,
                            const gfx::Rect *aClipRectIn,
                            const gfx::Rect& aRenderBounds,
                            bool aOpaque,
                            gfx::Rect *aClipRectOut /* = nullptr */,
                            gfx::Rect *aRenderBoundsOut /* = nullptr */)
{
  LayoutDeviceIntRect intRect(LayoutDeviceIntPoint(), mWidget->GetClientSize());
  Rect rect = Rect(0, 0, intRect.width, intRect.height);

  LayoutDeviceIntRegion invalidRegionSafe;
  if (mDidExternalComposition) {
    // We do not know rendered region during external composition, just redraw
    // whole widget.
    invalidRegionSafe = intRect;
    mDidExternalComposition = false;
  } else {
    // Sometimes the invalid region is larger than we want to draw.
    invalidRegionSafe.And(
      LayoutDeviceIntRegion::FromUnknownRegion(aInvalidRegion), intRect);
  }

  mInvalidRegion = invalidRegionSafe;
  mInvalidRect = mInvalidRegion.GetBounds();

  if (aRenderBoundsOut) {
    *aRenderBoundsOut = Rect();
  }

  BufferMode bufferMode = BufferMode::BUFFERED;
  if (mTarget) {
    // If we have a copy target, then we don't have a widget-provided mDrawTarget (currently). Use a dummy
    // placeholder so that CreateRenderTarget() works.
    mDrawTarget = gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  } else {
    // StartRemoteDrawingInRegion can mutate mInvalidRegion.
    mDrawTarget = mWidget->StartRemoteDrawingInRegion(mInvalidRegion, &bufferMode);
    if (!mDrawTarget) {
      return;
    }
    mInvalidRect = mInvalidRegion.GetBounds();
    if (mInvalidRect.IsEmpty()) {
      mWidget->EndRemoteDrawingInRegion(mDrawTarget, mInvalidRegion);
      return;
    }
  }

  if (!mDrawTarget || mInvalidRect.IsEmpty()) {
    return;
  }

  // Setup an intermediate render target to buffer all compositing. We will
  // copy this into mDrawTarget (the widget), and/or mTarget in EndFrame()
  RefPtr<CompositingRenderTarget> target =
    CreateRenderTargetForWindow(mInvalidRect,
                                aOpaque ? INIT_MODE_NONE : INIT_MODE_CLEAR,
                                bufferMode);
  if (!target) {
    if (!mTarget) {
      mWidget->EndRemoteDrawingInRegion(mDrawTarget, mInvalidRegion);
    }
    return;
  }
  SetRenderTarget(target);

  // We only allocate a surface sized to the invalidated region, so we need to
  // translate future coordinates.
  mRenderTarget->mDrawTarget->SetTransform(Matrix::Translation(-mRenderTarget->GetOrigin()));

  gfxUtils::ClipToRegion(mRenderTarget->mDrawTarget,
                         mInvalidRegion.ToUnknownRegion());

  if (aRenderBoundsOut) {
    *aRenderBoundsOut = rect;
  }

  if (aClipRectIn) {
    mRenderTarget->mDrawTarget->PushClipRect(*aClipRectIn);
  } else {
    mRenderTarget->mDrawTarget->PushClipRect(rect);
    if (aClipRectOut) {
      *aClipRectOut = rect;
    }
  }
}

void
BasicCompositor::EndFrame()
{
  // Pop aClipRectIn/bounds rect
  mRenderTarget->mDrawTarget->PopClip();

  if (gfxPrefs::WidgetUpdateFlashing()) {
    float r = float(rand()) / RAND_MAX;
    float g = float(rand()) / RAND_MAX;
    float b = float(rand()) / RAND_MAX;
    // We're still clipped to mInvalidRegion, so just fill the bounds.
    mRenderTarget->mDrawTarget->FillRect(
      IntRectToRect(mInvalidRegion.GetBounds()).ToUnknownRect(),
      ColorPattern(Color(r, g, b, 0.2f)));
  }

  // Pop aInvalidregion
  mRenderTarget->mDrawTarget->PopClip();

  if (mTarget || mRenderTarget->mDrawTarget != mDrawTarget) {
    // Note: Most platforms require us to buffer drawing to the widget surface.
    // That's why we don't draw to mDrawTarget directly.
    RefPtr<SourceSurface> source = mRenderTarget->mDrawTarget->Snapshot();
    RefPtr<DrawTarget> dest(mTarget ? mTarget : mDrawTarget);

    nsIntPoint offset = mTarget ? mTargetBounds.TopLeft() : nsIntPoint();

    // The source DrawTarget is clipped to the invalidation region, so we have
    // to copy the individual rectangles in the region or else we'll draw blank
    // pixels.
    for (auto iter = mInvalidRegion.RectIter(); !iter.Done(); iter.Next()) {
      const LayoutDeviceIntRect& r = iter.Get();
      dest->CopySurface(source,
                        IntRect(r.x, r.y, r.width, r.height) - mRenderTarget->GetOrigin(),
                        IntPoint(r.x, r.y) - offset);
    }
  }

  if (!mTarget) {
    mWidget->EndRemoteDrawingInRegion(mDrawTarget, mInvalidRegion);
  }

  mDrawTarget = nullptr;
  mRenderTarget = nullptr;
}

void
BasicCompositor::EndFrameForExternalComposition(const gfx::Matrix& aTransform)
{
  MOZ_ASSERT(!mTarget);
  MOZ_ASSERT(!mDrawTarget);
  MOZ_ASSERT(!mRenderTarget);

  mDidExternalComposition = true;
}


} // namespace layers
} // namespace mozilla
