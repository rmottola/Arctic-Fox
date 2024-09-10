/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "ImageContainer.h"
#include <string.h>                     // for memcpy, memset
#include "GLImages.h"                   // for SurfaceTextureImage
#include "gfx2DGlue.h"
#include "gfxPlatform.h"                // for gfxPlatform
#include "gfxUtils.h"                   // for gfxUtils
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/ipc/CrossProcessMutex.h"  // for CrossProcessMutex, etc
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/ImageBridgeChild.h"  // for ImageBridgeChild
#include "mozilla/layers/PImageContainerChild.h"
#include "mozilla/layers/ImageClient.h"  // for ImageClient
#include "mozilla/layers/LayersMessages.h"
#include "mozilla/layers/SharedPlanarYCbCrImage.h"
#include "mozilla/layers/SharedRGBImage.h"
#include "nsISupportsUtils.h"           // for NS_IF_ADDREF
#include "YCbCrUtils.h"                 // for YCbCr conversions
#ifdef MOZ_WIDGET_GONK
#include "GrallocImages.h"
#endif
#if defined(MOZ_WIDGET_GONK) && defined(MOZ_B2G_CAMERA) && defined(MOZ_WEBRTC)
#include "GonkCameraImage.h"
#endif
#include "gfx2DGlue.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/CheckedInt.h"

#ifdef XP_MACOSX
#include "mozilla/gfx/QuartzSupport.h"
#endif

#ifdef XP_WIN
#include "gfxWindowsPlatform.h"
#include <d3d10_1.h>
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::ipc;
using namespace android;
using namespace mozilla::gfx;

Atomic<int32_t> Image::sSerialCounter(0);

Atomic<uint32_t> ImageContainer::sGenerationCounter(0);

RefPtr<PlanarYCbCrImage>
ImageFactory::CreatePlanarYCbCrImage(const gfx::IntSize& aScaleHint, BufferRecycleBin *aRecycleBin)
{
  return new RecyclingPlanarYCbCrImage(aRecycleBin);
}

BufferRecycleBin::BufferRecycleBin()
  : mLock("mozilla.layers.BufferRecycleBin.mLock")
  // This member is only valid when the bin is not empty and will be properly
  // initialized in RecycleBuffer, but initializing it here avoids static analysis
  // noise.
  , mRecycledBufferSize(0)
{
}

void
BufferRecycleBin::RecycleBuffer(UniquePtr<uint8_t[]> aBuffer, uint32_t aSize)
{
  MutexAutoLock lock(mLock);

  if (!mRecycledBuffers.IsEmpty() && aSize != mRecycledBufferSize) {
    mRecycledBuffers.Clear();
  }
  mRecycledBufferSize = aSize;
  mRecycledBuffers.AppendElement(Move(aBuffer));
}

UniquePtr<uint8_t[]>
BufferRecycleBin::GetBuffer(uint32_t aSize)
{
  MutexAutoLock lock(mLock);

  if (mRecycledBuffers.IsEmpty() || mRecycledBufferSize != aSize)
    return MakeUnique<uint8_t[]>(aSize);

  uint32_t last = mRecycledBuffers.Length() - 1;
  UniquePtr<uint8_t[]> result = Move(mRecycledBuffers[last]);
  mRecycledBuffers.RemoveElementAt(last);
  return result;
}

/**
 * The child side of PImageContainer. It's best to avoid ImageContainer filling
 * this role since IPDL objects should be associated with a single thread and
 * ImageContainer definitely isn't. This object belongs to (and is always
 * destroyed on) the ImageBridge thread, except when we need to destroy it
 * during shutdown.
 * An ImageContainer owns one of these; we have a weak reference to our
 * ImageContainer.
 */
class ImageContainerChild : public PImageContainerChild {
public:
  explicit ImageContainerChild(ImageContainer* aImageContainer)
    : mLock("ImageContainerChild")
    , mImageContainer(aImageContainer)
    , mImageContainerReleased(false)
    , mIPCOpen(true)
  {}

  void ForgetImageContainer()
  {
    MutexAutoLock lock(mLock);
    mImageContainer = nullptr;
  }

  // This protects mImageContainer. This is always taken before the
  // mImageContainer's monitor (when both need to be held).
  Mutex mLock;
  ImageContainer* mImageContainer;
  // If mImageContainerReleased is false when we try to deallocate this actor,
  // it means the ImageContainer is still holding a pointer to this.
  // mImageContainerReleased must not be accessed off the ImageBridgeChild thread.
  bool mImageContainerReleased;
  // If mIPCOpen is false, it means the IPDL code tried to deallocate the actor
  // before the ImageContainer released it. When this happens we don't actually
  // delete the actor right away because the ImageContainer has a reference to
  // it. In this case the actor will be deleted when the ImageContainer lets go
  // of it.
  // mIPCOpen must not be accessed off the ImageBridgeChild thread.
  bool mIPCOpen;
};

// static
void
ImageContainer::DeallocActor(PImageContainerChild* aActor)
{
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(InImageBridgeChildThread());

  auto actor = static_cast<ImageContainerChild*>(aActor);
  if (actor->mImageContainerReleased) {
    delete actor;
  } else {
    actor->mIPCOpen = false;
  }
}

// static
void
ImageContainer::AsyncDestroyActor(PImageContainerChild* aActor)
{
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(InImageBridgeChildThread());

  auto actor = static_cast<ImageContainerChild*>(aActor);

  // Setting mImageContainerReleased to true means next time DeallocActor is
  // called, the actor will be deleted.
  actor->mImageContainerReleased = true;

  if (actor->mIPCOpen && ImageBridgeChild::IsCreated() && !ImageBridgeChild::IsShutDown()) {
    actor->SendAsyncDelete();
  } else {
    // The actor is already dead as far as IPDL is concerned, probably because
    // of a channel error. We can deallocate it now.
    DeallocActor(actor);
  }
}

ImageContainer::ImageContainer(Mode flag)
: mReentrantMonitor("ImageContainer.mReentrantMonitor"),
  mGenerationCounter(++sGenerationCounter),
  mPaintCount(0),
  mDroppedImageCount(0),
  mImageFactory(new ImageFactory()),
  mRecycleBin(new BufferRecycleBin()),
  mImageClient(nullptr),
  mCurrentProducerID(-1),
  mIPDLChild(nullptr)
{
  if (ImageBridgeChild::IsCreated()) {
    // the refcount of this ImageClient is 1. we don't use a RefPtr here because the refcount
    // of this class must be done on the ImageBridge thread.
    switch (flag) {
      case SYNCHRONOUS:
        break;
      case ASYNCHRONOUS:
        mIPDLChild = new ImageContainerChild(this);
        mImageClient = ImageBridgeChild::GetSingleton()->CreateImageClient(CompositableType::IMAGE, this).take();
        MOZ_ASSERT(mImageClient);
        break;
      default:
        MOZ_ASSERT(false, "This flag is invalid.");
        break;
    }
  }
}

ImageContainer::~ImageContainer()
{
  if (IsAsync()) {
    mIPDLChild->ForgetImageContainer();
    ImageBridgeChild::DispatchReleaseImageClient(mImageClient, mIPDLChild);
  }
}

RefPtr<PlanarYCbCrImage>
ImageContainer::CreatePlanarYCbCrImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (mImageClient && mImageClient->AsImageClientSingle()) {
    return new SharedPlanarYCbCrImage(mImageClient);
  }
  return mImageFactory->CreatePlanarYCbCrImage(mScaleHint, mRecycleBin);
}

RefPtr<SharedRGBImage>
ImageContainer::CreateSharedRGBImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (!mImageClient || !mImageClient->AsImageClientSingle()) {
    return nullptr;
  }
  return new SharedRGBImage(mImageClient);
}

#ifdef MOZ_WIDGET_GONK
RefPtr<OverlayImage>
ImageContainer::CreateOverlayImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (!mImageClient || !mImageClient->AsImageClientSingle()) {
    return nullptr;
  }
  return new OverlayImage();
}
#endif

void
ImageContainer::SetCurrentImageInternal(const nsTArray<NonOwningImage>& aImages)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  mGenerationCounter = ++sGenerationCounter;

  if (!aImages.IsEmpty()) {
    NS_ASSERTION(mCurrentImages.IsEmpty() ||
                 mCurrentImages[0].mProducerID != aImages[0].mProducerID ||
                 mCurrentImages[0].mFrameID <= aImages[0].mFrameID,
                 "frame IDs shouldn't go backwards");
    if (aImages[0].mProducerID != mCurrentProducerID) {
      mFrameIDsNotYetComposited.Clear();
      mCurrentProducerID = aImages[0].mProducerID;
    } else if (!aImages[0].mTimeStamp.IsNull()) {
      // Check for expired frames
      for (auto& img : mCurrentImages) {
        if (img.mProducerID != aImages[0].mProducerID ||
            img.mTimeStamp.IsNull() ||
            img.mTimeStamp >= aImages[0].mTimeStamp) {
          break;
        }
        if (!img.mComposited && !img.mTimeStamp.IsNull() &&
            img.mFrameID != aImages[0].mFrameID) {
          mFrameIDsNotYetComposited.AppendElement(img.mFrameID);
        }
      }

      // Remove really old frames, assuming they'll never be composited.
      const uint32_t maxFrames = 100;
      if (mFrameIDsNotYetComposited.Length() > maxFrames) {
        uint32_t dropFrames = mFrameIDsNotYetComposited.Length() - maxFrames;
        mDroppedImageCount += dropFrames;
        mFrameIDsNotYetComposited.RemoveElementsAt(0, dropFrames);
      }
    }
  }

  nsTArray<OwningImage> newImages;

  for (uint32_t i = 0; i < aImages.Length(); ++i) {
    NS_ASSERTION(aImages[i].mImage, "image can't be null");
    NS_ASSERTION(!aImages[i].mTimeStamp.IsNull() || aImages.Length() == 1,
                 "Multiple images require timestamps");
    if (i > 0) {
      NS_ASSERTION(aImages[i].mTimeStamp >= aImages[i - 1].mTimeStamp,
                   "Timestamps must not decrease");
      NS_ASSERTION(aImages[i].mFrameID > aImages[i - 1].mFrameID,
                   "FrameIDs must increase");
      NS_ASSERTION(aImages[i].mProducerID == aImages[i - 1].mProducerID,
                   "ProducerIDs must be the same");
    }
    OwningImage* img = newImages.AppendElement();
    img->mImage = aImages[i].mImage;
    img->mTimeStamp = aImages[i].mTimeStamp;
    img->mFrameID = aImages[i].mFrameID;
    img->mProducerID = aImages[i].mProducerID;
    for (auto& oldImg : mCurrentImages) {
      if (oldImg.mFrameID == img->mFrameID &&
          oldImg.mProducerID == img->mProducerID) {
        img->mComposited = oldImg.mComposited;
        break;
      }
    }
  }

  mCurrentImages.SwapElements(newImages);
}

void
ImageContainer::ClearImagesFromImageBridge()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  SetCurrentImageInternal(nsTArray<NonOwningImage>());
}

void
ImageContainer::SetCurrentImages(const nsTArray<NonOwningImage>& aImages)
{
  MOZ_ASSERT(!aImages.IsEmpty());
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  if (IsAsync()) {
    ImageBridgeChild::DispatchImageClientUpdate(mImageClient, this);
  }
  SetCurrentImageInternal(aImages);
}

 void
ImageContainer::ClearAllImages()
{
  if (IsAsync()) {
    // Let ImageClient release all TextureClients. This doesn't return
    // until ImageBridge has called ClearCurrentImageFromImageBridge.
    ImageBridgeChild::FlushAllImages(mImageClient, this);
    return;
  }

  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  SetCurrentImageInternal(nsTArray<NonOwningImage>());
}

void
ImageContainer::SetCurrentImageInTransaction(Image *aImage)
{
  AutoTArray<NonOwningImage,1> images;
  images.AppendElement(NonOwningImage(aImage));
  SetCurrentImagesInTransaction(images);
}

void
ImageContainer::SetCurrentImagesInTransaction(const nsTArray<NonOwningImage>& aImages)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ASSERTION(!mImageClient, "Should use async image transfer with ImageBridge.");

  SetCurrentImageInternal(aImages);
}

bool ImageContainer::IsAsync() const
{
  return mImageClient != nullptr;
}

uint64_t ImageContainer::GetAsyncContainerID() const
{
  NS_ASSERTION(IsAsync(),"Shared image ID is only relevant to async ImageContainers");
  if (IsAsync()) {
    return mImageClient->GetAsyncID();
  } else {
    return 0; // zero is always an invalid AsyncID
  }
}

bool
ImageContainer::HasCurrentImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  return !mCurrentImages.IsEmpty();
}

void
ImageContainer::GetCurrentImages(nsTArray<OwningImage>* aImages,
                                 uint32_t* aGenerationCounter)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  *aImages = mCurrentImages;
  if (aGenerationCounter) {
    *aGenerationCounter = mGenerationCounter;
  }
}

gfx::IntSize
ImageContainer::GetCurrentSize()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mCurrentImages.IsEmpty()) {
    return gfx::IntSize(0, 0);
  }

  return mCurrentImages[0].mImage->GetSize();
}

void
ImageContainer::NotifyCompositeInternal(const ImageCompositeNotification& aNotification)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  // An image composition notification is sent the first time a particular
  // image is composited by an ImageHost. Thus, every time we receive such
  // a notification, a new image has been painted.
  ++mPaintCount;

  if (aNotification.producerID() == mCurrentProducerID) {
    uint32_t i;
    for (i = 0; i < mFrameIDsNotYetComposited.Length(); ++i) {
      if (mFrameIDsNotYetComposited[i] <= aNotification.frameID()) {
        if (mFrameIDsNotYetComposited[i] < aNotification.frameID()) {
          ++mDroppedImageCount;
        }
      } else {
        break;
      }
    }
    mFrameIDsNotYetComposited.RemoveElementsAt(0, i);
    for (auto& img : mCurrentImages) {
      if (img.mFrameID == aNotification.frameID()) {
        img.mComposited = true;
      }
    }
  }

  if (!aNotification.imageTimeStamp().IsNull()) {
    mPaintDelay = aNotification.firstCompositeTimeStamp() -
        aNotification.imageTimeStamp();
  }
}

PlanarYCbCrImage::PlanarYCbCrImage()
  : Image(nullptr, ImageFormat::PLANAR_YCBCR)
  , mOffscreenFormat(SurfaceFormat::UNKNOWN)
  , mBufferSize(0)
{
}

RecyclingPlanarYCbCrImage::~RecyclingPlanarYCbCrImage()
{
  if (mBuffer) {
    mRecycleBin->RecycleBuffer(Move(mBuffer), mBufferSize);
  }
}

size_t
RecyclingPlanarYCbCrImage::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  // Ignoring:
  // - mData - just wraps mBuffer
  // - Surfaces should be reported under gfx-surfaces-*:
  //   - mSourceSurface
  // - Base class:
  //   - mImplData is not used
  // Not owned:
  // - mRecycleBin
  size_t size = aMallocSizeOf(mBuffer.get());

  // Could add in the future:
  // - mBackendData (from base class)

  return size;
}

UniquePtr<uint8_t[]>
RecyclingPlanarYCbCrImage::AllocateBuffer(uint32_t aSize)
{
  return mRecycleBin->GetBuffer(aSize);
}

static void
CopyPlane(uint8_t *aDst, const uint8_t *aSrc,
          const gfx::IntSize &aSize, int32_t aStride, int32_t aSkip)
{
  int32_t height = aSize.height;
  int32_t width = aSize.width;

  MOZ_RELEASE_ASSERT(width <= aStride);

  if (!aSkip) {
    // Fast path: planar input.
    memcpy(aDst, aSrc, height * aStride);
  } else {
    for (int y = 0; y < height; ++y) {
      const uint8_t *src = aSrc;
      uint8_t *dst = aDst;
      // Slow path
      for (int x = 0; x < width; ++x) {
        *dst++ = *src++;
        src += aSkip;
      }
      aSrc += aStride;
      aDst += aStride;
    }
  }
}

bool
RecyclingPlanarYCbCrImage::CopyData(const Data& aData)
{
  mData = aData;

  // update buffer size
  // Use uint32_t throughout to match AllocateBuffer's param and mBufferSize
  const auto checkedSize =
    CheckedInt<uint32_t>(aData.mCbCrStride) * aData.mCbCrSize.height * 2 +
    CheckedInt<uint32_t>(aData.mYStride) * aData.mYSize.height;

  if (!checkedSize.isValid())
    return false;

  const auto size = checkedSize.value();

  // get new buffer
  mBuffer = AllocateBuffer(size);
  if (!mBuffer)
    return false;

  // update buffer size
  mBufferSize = size;

  mData.mYChannel = mBuffer.get();
  mData.mCbChannel = mData.mYChannel + mData.mYStride * mData.mYSize.height;
  mData.mCrChannel = mData.mCbChannel + mData.mCbCrStride * mData.mCbCrSize.height;

  CopyPlane(mData.mYChannel, aData.mYChannel,
            mData.mYSize, mData.mYStride, mData.mYSkip);
  CopyPlane(mData.mCbChannel, aData.mCbChannel,
            mData.mCbCrSize, mData.mCbCrStride, mData.mCbSkip);
  CopyPlane(mData.mCrChannel, aData.mCrChannel,
            mData.mCbCrSize, mData.mCbCrStride, mData.mCrSkip);

  mSize = aData.mPicSize;
  return true;
}

gfxImageFormat
PlanarYCbCrImage::GetOffscreenFormat()
{
  return mOffscreenFormat == SurfaceFormat::UNKNOWN ?
    gfxPlatform::GetPlatform()->GetOffscreenFormat() :
    mOffscreenFormat;
}

bool
PlanarYCbCrImage::AdoptData(const Data &aData)
{
  mData = aData;
  mSize = aData.mPicSize;
  return true;
}

uint8_t*
RecyclingPlanarYCbCrImage::AllocateAndGetNewBuffer(uint32_t aSize)
{
  // get new buffer
  mBuffer = AllocateBuffer(aSize);
  if (mBuffer) {
    // update buffer size
    mBufferSize = aSize;
  }
  return mBuffer.get();
}

already_AddRefed<gfx::SourceSurface>
PlanarYCbCrImage::GetAsSourceSurface()
{
  if (mSourceSurface) {
    RefPtr<gfx::SourceSurface> surface(mSourceSurface);
    return surface.forget();
  }

  gfx::IntSize size(mSize);
  gfx::SurfaceFormat format = gfx::ImageFormatToSurfaceFormat(GetOffscreenFormat());
  gfx::GetYCbCrToRGBDestFormatAndSize(mData, format, size);
  if (mSize.width > PlanarYCbCrImage::MAX_DIMENSION ||
      mSize.height > PlanarYCbCrImage::MAX_DIMENSION) {
    NS_ERROR("Illegal image dest width or height");
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> surface = gfx::Factory::CreateDataSourceSurface(size, format);
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap mapping(surface, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!mapping.IsMapped())) {
    return nullptr;
  }

  gfx::ConvertYCbCrToRGB(mData, format, size, mapping.GetData(), mapping.GetStride());

  mSourceSurface = surface;

  return surface.forget();
}

SourceSurfaceImage::SourceSurfaceImage(const gfx::IntSize& aSize, gfx::SourceSurface* aSourceSurface)
  : Image(nullptr, ImageFormat::CAIRO_SURFACE),
    mSize(aSize),
    mSourceSurface(aSourceSurface)
{}

SourceSurfaceImage::SourceSurfaceImage(gfx::SourceSurface* aSourceSurface)
  : Image(nullptr, ImageFormat::CAIRO_SURFACE),
    mSize(aSourceSurface->GetSize()),
    mSourceSurface(aSourceSurface)
{}

SourceSurfaceImage::~SourceSurfaceImage()
{
}

TextureClient*
SourceSurfaceImage::GetTextureClient(CompositableClient *aClient)
{
  if (!aClient) {
    return nullptr;
  }

  CompositableForwarder* forwarder = aClient->GetForwarder();
  RefPtr<TextureClient> textureClient = mTextureClients.Get(forwarder->GetSerial());
  if (textureClient) {
    return textureClient;
  }

  RefPtr<SourceSurface> surface = GetAsSourceSurface();
  MOZ_ASSERT(surface);
  if (!surface) {
    return nullptr;
  }


// XXX windows' TextureClients do not hold ISurfaceAllocator,
// recycler does not work on windows.
#ifndef XP_WIN

// XXX only gonk ensure when TextureClient is recycled,
// TextureHost is not used by CompositableHost.
#ifdef MOZ_WIDGET_GONK
  RefPtr<TextureClientRecycleAllocator> recycler =
    aClient->GetTextureClientRecycler();
  if (recycler) {
    textureClient =
      recycler->CreateOrRecycle(surface->GetFormat(),
                                surface->GetSize(),
                                BackendSelector::Content,
                                aClient->GetTextureFlags());
  }
#endif

#endif
  if (!textureClient) {
    // gfx::BackendType::NONE means default to content backend
    textureClient = aClient->CreateTextureClientForDrawing(surface->GetFormat(),
                                                           surface->GetSize(),
                                                           BackendSelector::Content,
                                                           TextureFlags::DEFAULT);
  }
  if (!textureClient) {
    return nullptr;
  }

  TextureClientAutoLock autoLock(textureClient, OpenMode::OPEN_WRITE_ONLY);
  if (!autoLock.Succeeded()) {
    return nullptr;
  }

  textureClient->UpdateFromSurface(surface);

  textureClient->SyncWithObject(forwarder->GetSyncObject());

  mTextureClients.Put(forwarder->GetSerial(), textureClient);
  return textureClient;
}

PImageContainerChild*
ImageContainer::GetPImageContainerChild()
{
  return mIPDLChild;
}

/* static */ void
ImageContainer::NotifyComposite(const ImageCompositeNotification& aNotification)
{
  ImageContainerChild* child =
      static_cast<ImageContainerChild*>(aNotification.imageContainerChild());
  if (child) {
    MutexAutoLock lock(child->mLock);
    if (child->mImageContainer) {
      child->mImageContainer->NotifyCompositeInternal(aNotification);
    }
  }
}

ImageContainer::ProducerID
ImageContainer::AllocateProducerID()
{
  // Callable on all threads.
  static Atomic<ImageContainer::ProducerID> sProducerID(0u);
  return ++sProducerID;
}

} // namespace layers
} // namespace mozilla
