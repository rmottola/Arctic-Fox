/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IDecodingTask.h"

#include "gfxPrefs.h"
#include "nsThreadUtils.h"

#include "Decoder.h"
#include "DecodePool.h"
#include "RasterImage.h"
#include "SurfaceCache.h"

namespace mozilla {

using gfx::IntRect;

namespace image {

///////////////////////////////////////////////////////////////////////////////
// Helpers for sending notifications to the image associated with a decoder.
///////////////////////////////////////////////////////////////////////////////

/* static */ void
IDecodingTask::NotifyProgress(NotNull<RasterImage*> aImage,
                              NotNull<Decoder*> aDecoder)
{
  MOZ_ASSERT(aDecoder->HasProgress() && !aDecoder->IsMetadataDecode());

  // Capture the decoder's state. If we need to notify asynchronously, it's
  // important that we don't wait until the lambda actually runs to capture the
  // state that we're going to notify. That would both introduce data races on
  // the decoder's state and cause inconsistencies between the NotifyProgress()
  // calls we make off-main-thread and the notifications that RasterImage
  // actually receives, which would cause bugs.
  Progress progress = aDecoder->TakeProgress();
  IntRect invalidRect = aDecoder->TakeInvalidRect();
  Maybe<uint32_t> frameCount = aDecoder->TakeCompleteFrameCount();
  SurfaceFlags surfaceFlags = aDecoder->GetSurfaceFlags();

  // Synchronously notify if we can.
  if (NS_IsMainThread() &&
      !(aDecoder->GetDecoderFlags() & DecoderFlags::ASYNC_NOTIFY)) {
    aImage->NotifyProgress(progress, invalidRect,
                           frameCount, surfaceFlags);
    return;
  }

  // We're forced to notify asynchronously.
  NotNull<RefPtr<RasterImage>> image = aImage;
  NS_DispatchToMainThread(NS_NewRunnableFunction([=]() -> void {
    image->NotifyProgress(progress, invalidRect,
                          frameCount, surfaceFlags);
  }));
}

/* static */ void
IDecodingTask::NotifyDecodeComplete(NotNull<RasterImage*> aImage,
                                    NotNull<Decoder*> aDecoder)
{
  MOZ_ASSERT(aDecoder->HasError() || !aDecoder->InFrame(),
             "Decode complete in the middle of a frame?");

  // Capture the decoder's state.
  DecoderFinalStatus finalStatus = aDecoder->FinalStatus();
  ImageMetadata metadata = aDecoder->GetImageMetadata();
  DecoderTelemetry telemetry = aDecoder->Telemetry();
  Progress progress = aDecoder->TakeProgress();
  IntRect invalidRect = aDecoder->TakeInvalidRect();
  Maybe<uint32_t> frameCount = aDecoder->TakeCompleteFrameCount();
  SurfaceFlags surfaceFlags = aDecoder->GetSurfaceFlags();

  // Synchronously notify if we can.
  if (NS_IsMainThread() &&
      !(aDecoder->GetDecoderFlags() & DecoderFlags::ASYNC_NOTIFY)) {
    aImage->NotifyDecodeComplete(finalStatus, metadata, telemetry, progress,
                                 invalidRect, frameCount, surfaceFlags);
    return;
  }

  // We're forced to notify asynchronously.
  NotNull<RefPtr<RasterImage>> image = aImage;
  NS_DispatchToMainThread(NS_NewRunnableFunction([=]() -> void {
    image->NotifyDecodeComplete(finalStatus, metadata, telemetry, progress,
                                invalidRect, frameCount, surfaceFlags);
  }));
}


///////////////////////////////////////////////////////////////////////////////
// IDecodingTask implementation.
///////////////////////////////////////////////////////////////////////////////

void
IDecodingTask::Resume()
{
  DecodePool::Singleton()->AsyncRun(this);
}


///////////////////////////////////////////////////////////////////////////////
// AnimationDecodingTask implementation.
///////////////////////////////////////////////////////////////////////////////

AnimationDecodingTask::AnimationDecodingTask(NotNull<Decoder*> aDecoder)
  : mDecoder(aDecoder)
{
  MOZ_ASSERT(!mDecoder->IsMetadataDecode(),
             "Use MetadataDecodingTask for metadata decodes");
  MOZ_ASSERT(!mDecoder->IsFirstFrameDecode(),
             "Use DecodingTask for single-frame image decodes");
}

void
AnimationDecodingTask::Run()
{
  while (true) {
    LexerResult result = mDecoder->Decode(WrapNotNull(this));

    if (result.is<TerminalState>()) {
      NotifyDecodeComplete(mDecoder->GetImage(), mDecoder);
      return;  // We're done.
    }

    MOZ_ASSERT(result.is<Yield>());

    // Notify for the progress we've made so far.
    if (mDecoder->HasProgress()) {
      NotifyProgress(mDecoder->GetImage(), mDecoder);
    }

    if (result == LexerResult(Yield::NEED_MORE_DATA)) {
      // We can't make any more progress right now. The decoder itself will
      // ensure that we get reenqueued when more data is available; just return
      // for now.
      return;
    }

    // Right now we don't do anything special for other kinds of yields, so just
    // keep working.
  }
}

bool
AnimationDecodingTask::ShouldPreferSyncRun() const
{
  return mDecoder->ShouldSyncDecode(gfxPrefs::ImageMemDecodeBytesAtATime());
}


///////////////////////////////////////////////////////////////////////////////
// MetadataDecodingTask implementation.
///////////////////////////////////////////////////////////////////////////////

MetadataDecodingTask::MetadataDecodingTask(NotNull<Decoder*> aDecoder)
  : mDecoder(aDecoder)
{
  MOZ_ASSERT(mDecoder->IsMetadataDecode(),
             "Use DecodingTask for non-metadata decodes");
}

void
MetadataDecodingTask::Run()
{
  LexerResult result = mDecoder->Decode(WrapNotNull(this));

  if (result.is<TerminalState>()) {
    NotifyDecodeComplete(mDecoder->GetImage(), mDecoder);
    return;  // We're done.
  }

  if (result == LexerResult(Yield::NEED_MORE_DATA)) {
    // We can't make any more progress right now. We also don't want to report
    // any progress, because it's important that metadata decode results are
    // delivered atomically. The decoder itself will ensure that we get
    // reenqueued when more data is available; just return for now.
    return;
  }

  MOZ_ASSERT_UNREACHABLE("Metadata decode yielded for an unexpected reason");
}


///////////////////////////////////////////////////////////////////////////////
// AnonymousDecodingTask implementation.
///////////////////////////////////////////////////////////////////////////////

AnonymousDecodingTask::AnonymousDecodingTask(NotNull<Decoder*> aDecoder)
  : mDecoder(aDecoder)
{ }

void
AnonymousDecodingTask::Run()
{
  while (true) {
    LexerResult result = mDecoder->Decode(WrapNotNull(this));

    if (result.is<TerminalState>()) {
      return;  // We're done.
    }

    if (result == LexerResult(Yield::NEED_MORE_DATA)) {
      // We can't make any more progress right now. Let the caller decide how to
      // handle it.
      return;
    }

    // Right now we don't do anything special for other kinds of yields, so just
    // keep working.
    MOZ_ASSERT(result.is<Yield>());
  }
}

} // namespace image
} // namespace mozilla
