/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AppleVDADecoder_h
#define mozilla_AppleVDADecoder_h

#include "PlatformDecoderModule.h"
#include "mozilla/Atomics.h"
#include "mozilla/ReentrantMonitor.h"
#include "MP4Decoder.h"
#include "nsIThread.h"
#include "ReorderQueue.h"
#include "TimeUnits.h"

#include "VideoDecodeAcceleration/VDADecoder.h"

namespace mozilla {

class FlushableTaskQueue;
class MediaDataDecoderCallback;
namespace layers {
  class ImageContainer;
} // namespace layers

class AppleVDADecoder : public MediaDataDecoder {
public:
  class AppleFrameRef {
  public:
    media::TimeUnit decode_timestamp;
    media::TimeUnit composition_timestamp;
    media::TimeUnit duration;
    int64_t byte_offset;
    bool is_sync_point;

    explicit AppleFrameRef(const MediaRawData& aSample)
      : decode_timestamp(media::TimeUnit::FromMicroseconds(aSample.mTimecode))
      , composition_timestamp(media::TimeUnit::FromMicroseconds(aSample.mTime))
      , duration(media::TimeUnit::FromMicroseconds(aSample.mDuration))
      , byte_offset(aSample.mOffset)
      , is_sync_point(aSample.mKeyframe)
    {
    }

    AppleFrameRef(const media::TimeUnit& aDts,
                  const media::TimeUnit& aPts,
                  const media::TimeUnit& aDuration,
                  int64_t aByte_offset,
                  bool aIs_sync_point)
      : decode_timestamp(aDts)
      , composition_timestamp(aPts)
      , duration(aDuration)
      , byte_offset(aByte_offset)
      , is_sync_point(aIs_sync_point)
    {
    }
  };

  // Return a new created AppleVDADecoder or nullptr if media or hardware is
  // not supported by current configuration.
  static already_AddRefed<AppleVDADecoder> CreateVDADecoder(
    const VideoInfo& aConfig,
    FlushableTaskQueue* aVideoTaskQueue,
    MediaDataDecoderCallback* aCallback,
    layers::ImageContainer* aImageContainer);

  AppleVDADecoder(const VideoInfo& aConfig,
                  FlushableTaskQueue* aVideoTaskQueue,
                  MediaDataDecoderCallback* aCallback,
                  layers::ImageContainer* aImageContainer);
  virtual ~AppleVDADecoder();
  RefPtr<InitPromise> Init() override;
  nsresult Input(MediaRawData* aSample) override;
  nsresult Flush() override;
  nsresult Drain() override;
  nsresult Shutdown() override;
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override
  {
    return true;
  }

  const char* GetDescriptionName() const override
  {
    return "apple VDA decoder";
  }

  // Access from the taskqueue and the decoder's thread.
  // OutputFrame is thread-safe.
  nsresult OutputFrame(CVPixelBufferRef aImage,
                       AppleFrameRef aFrameRef);

protected:
  // Flush and Drain operation, always run
  virtual void ProcessFlush();
  virtual void ProcessDrain();
  virtual void ProcessShutdown();

  AppleFrameRef* CreateAppleFrameRef(const MediaRawData* aSample);
  void DrainReorderedFrames();
  void ClearReorderedFrames();
  CFDictionaryRef CreateOutputConfiguration();

  RefPtr<MediaByteBuffer> mExtraData;
  RefPtr<FlushableTaskQueue> mTaskQueue;
  MediaDataDecoderCallback* mCallback;
  RefPtr<layers::ImageContainer> mImageContainer;
  uint32_t mPictureWidth;
  uint32_t mPictureHeight;
  uint32_t mDisplayWidth;
  uint32_t mDisplayHeight;
  // Accessed on multiple threads, but only set in constructor.
  uint32_t mMaxRefFrames;
  // Increased when Input is called, and decreased when ProcessFrame runs.
  // Reaching 0 indicates that there's no pending Input.
  Atomic<uint32_t> mInputIncoming;
  Atomic<bool> mIsShutDown;

  const bool mUseSoftwareImages;
  const bool mIs106;

  // Number of times a sample was queued via Input(). Will be decreased upon
  // the decoder's callback being invoked.
  // This is used to calculate how many frames has been buffered by the decoder.
  Atomic<uint32_t> mQueuedSamples;

  // Protects mReorderQueue.
  Monitor mMonitor;
  // Set on reader/decode thread calling Flush() to indicate that output is
  // not required and so input samples on mTaskQueue need not be processed.
  // Cleared on mTaskQueue in ProcessDrain().
  Atomic<bool> mIsFlushing;
  ReorderQueue mReorderQueue;

private:
  VDADecoder mDecoder;

  // Method to set up the decompression session.
  nsresult InitializeSession();

  // Method to pass a frame to VideoToolbox for decoding.
  nsresult SubmitFrame(MediaRawData* aSample);
  CFDictionaryRef CreateDecoderSpecification();
};

} // namespace mozilla

#endif // mozilla_AppleVDADecoder_h
