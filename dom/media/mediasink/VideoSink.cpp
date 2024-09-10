/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaQueue.h"
#include "VideoSink.h"

namespace mozilla {

extern LazyLogModule gMediaDecoderLog;
#define VSINK_LOG(msg, ...) \
  MOZ_LOG(gMediaDecoderLog, LogLevel::Debug, \
    ("VideoSink=%p " msg, this, ##__VA_ARGS__))
#define VSINK_LOG_V(msg, ...) \
  MOZ_LOG(gMediaDecoderLog, LogLevel::Verbose, \
  ("VideoSink=%p " msg, this, ##__VA_ARGS__))

using namespace mozilla::layers;

namespace media {

VideoSink::VideoSink(AbstractThread* aThread,
                     MediaSink* aAudioSink,
                     MediaQueue<MediaData>& aVideoQueue,
                     VideoFrameContainer* aContainer,
                     FrameStatistics& aFrameStats,
                     uint32_t aVQueueSentToCompositerSize)
  : mOwnerThread(aThread)
  , mAudioSink(aAudioSink)
  , mVideoQueue(aVideoQueue)
  , mContainer(aContainer)
  , mProducerID(ImageContainer::AllocateProducerID())
  , mFrameStats(aFrameStats)
  , mVideoFrameEndTime(-1)
  , mHasVideo(false)
  , mUpdateScheduler(aThread)
  , mVideoQueueSendToCompositorSize(aVQueueSentToCompositerSize)
{
  MOZ_ASSERT(mAudioSink, "AudioSink should exist.");
}

VideoSink::~VideoSink()
{
}

const MediaSink::PlaybackParams&
VideoSink::GetPlaybackParams() const
{
  AssertOwnerThread();

  return mAudioSink->GetPlaybackParams();
}

void
VideoSink::SetPlaybackParams(const PlaybackParams& aParams)
{
  AssertOwnerThread();

  mAudioSink->SetPlaybackParams(aParams);
}

RefPtr<GenericPromise>
VideoSink::OnEnded(TrackType aType)
{
  AssertOwnerThread();
  MOZ_ASSERT(mAudioSink->IsStarted(), "Must be called after playback starts.");

  if (aType == TrackInfo::kAudioTrack) {
    return mAudioSink->OnEnded(aType);
  } else if (aType == TrackInfo::kVideoTrack) {
    return mEndPromise;
  }
  return nullptr;
}

int64_t
VideoSink::GetEndTime(TrackType aType) const
{
  AssertOwnerThread();
  MOZ_ASSERT(mAudioSink->IsStarted(), "Must be called after playback starts.");

  if (aType == TrackInfo::kVideoTrack) {
    return mVideoFrameEndTime;
  } else if (aType == TrackInfo::kAudioTrack) {
    return mAudioSink->GetEndTime(aType);
  }
  return -1;
}

int64_t
VideoSink::GetPosition(TimeStamp* aTimeStamp) const
{
  AssertOwnerThread();

  return mAudioSink->GetPosition(aTimeStamp);
}

bool
VideoSink::HasUnplayedFrames(TrackType aType) const
{
  AssertOwnerThread();
  MOZ_ASSERT(aType == TrackInfo::kAudioTrack, "Not implemented for non audio tracks.");

  return mAudioSink->HasUnplayedFrames(aType);
}

void
VideoSink::SetPlaybackRate(double aPlaybackRate)
{
  AssertOwnerThread();

  mAudioSink->SetPlaybackRate(aPlaybackRate);
}

void
VideoSink::SetVolume(double aVolume)
{
  AssertOwnerThread();

  mAudioSink->SetVolume(aVolume);
}

void
VideoSink::SetPreservesPitch(bool aPreservesPitch)
{
  AssertOwnerThread();

  mAudioSink->SetPreservesPitch(aPreservesPitch);
}

void
VideoSink::SetPlaying(bool aPlaying)
{
  AssertOwnerThread();
  VSINK_LOG_V(" playing (%d) -> (%d)", mAudioSink->IsPlaying(), aPlaying);

  if (!aPlaying) {
    // Reset any update timer if paused.
    mUpdateScheduler.Reset();
    // Since playback is paused, tell compositor to render only current frame.
    RenderVideoFrames(1);
  }

  mAudioSink->SetPlaying(aPlaying);

  if (mHasVideo && aPlaying) {
    // There's no thread in VideoSink for pulling video frames, need to trigger
    // rendering while becoming playing status. because the VideoQueue may be
    // full already.
    TryUpdateRenderedVideoFrames();
  }
}

void
VideoSink::Start(int64_t aStartTime, const MediaInfo& aInfo)
{
  AssertOwnerThread();
  VSINK_LOG("[%s]", __func__);

  mAudioSink->Start(aStartTime, aInfo);

  mHasVideo = aInfo.HasVideo();

  if (mHasVideo) {
    mEndPromise = mEndPromiseHolder.Ensure(__func__);

    // If the underlying MediaSink has an end promise for the video track (which
    // happens when mAudioSink refers to a DecodedStream), we must wait for it
    // to complete before resolving our own end promise. Otherwise, MDSM might
    // stop playback before DecodedStream plays to the end and cause
    // test_streams_element_capture.html to time out.
    RefPtr<GenericPromise> p = mAudioSink->OnEnded(TrackInfo::kVideoTrack);
    if (p) {
      RefPtr<VideoSink> self = this;
      mVideoSinkEndRequest.Begin(p->Then(mOwnerThread, __func__,
        [self] () {
          self->mVideoSinkEndRequest.Complete();
          self->TryUpdateRenderedVideoFrames();
        }, [self] () {
          self->mVideoSinkEndRequest.Complete();
          self->TryUpdateRenderedVideoFrames();
        }));
    }

    ConnectListener();
    // Run the render loop at least once so we can resolve the end promise
    // when video duration is 0.
    UpdateRenderedVideoFrames();
  }
}

void
VideoSink::Stop()
{
  AssertOwnerThread();
  MOZ_ASSERT(mAudioSink->IsStarted(), "playback not started.");
  VSINK_LOG("[%s]", __func__);

  mAudioSink->Stop();

  mUpdateScheduler.Reset();
  if (mHasVideo) {
    DisconnectListener();
    mVideoSinkEndRequest.DisconnectIfExists();
    mEndPromiseHolder.ResolveIfExists(true, __func__);
    mEndPromise = nullptr;
  }
  mVideoFrameEndTime = -1;
}

bool
VideoSink::IsStarted() const
{
  AssertOwnerThread();

  return mAudioSink->IsStarted();
}

bool
VideoSink::IsPlaying() const
{
  AssertOwnerThread();

  return mAudioSink->IsPlaying();
}

void
VideoSink::Shutdown()
{
  AssertOwnerThread();
  MOZ_ASSERT(!mAudioSink->IsStarted(), "must be called after playback stops.");
  VSINK_LOG("[%s]", __func__);

  mAudioSink->Shutdown();
}

void
VideoSink::OnVideoQueuePushed(RefPtr<MediaData>&& aSample)
{
  AssertOwnerThread();
  // Listen to push event, VideoSink should try rendering ASAP if first frame
  // arrives but update scheduler is not triggered yet.
  VideoData* v = aSample->As<VideoData>();
  if (!v->mSentToCompositor) {
    // Since we push rendered frames back to the queue, we will receive
    // push events for them. We only need to trigger render loop
    // when this frame is not rendered yet.
    TryUpdateRenderedVideoFrames();
  }
}

void
VideoSink::OnVideoQueueFinished()
{
  AssertOwnerThread();
  // Run render loop if the end promise is not resolved yet.
  if (!mUpdateScheduler.IsScheduled() &&
      mAudioSink->IsPlaying() &&
      !mEndPromiseHolder.IsEmpty()) {
    UpdateRenderedVideoFrames();
  }
}

void
VideoSink::Redraw()
{
  AssertOwnerThread();
  RenderVideoFrames(1);
}

void
VideoSink::TryUpdateRenderedVideoFrames()
{
  AssertOwnerThread();
  if (!mUpdateScheduler.IsScheduled() && VideoQueue().GetSize() >= 1 &&
      mAudioSink->IsPlaying()) {
    UpdateRenderedVideoFrames();
  }
}

void
VideoSink::UpdateRenderedVideoFramesByTimer()
{
  AssertOwnerThread();
  mUpdateScheduler.CompleteRequest();
  UpdateRenderedVideoFrames();
}

void
VideoSink::ConnectListener()
{
  AssertOwnerThread();
  mPushListener = VideoQueue().PushEvent().Connect(
    mOwnerThread, this, &VideoSink::OnVideoQueuePushed);
  mFinishListener = VideoQueue().FinishEvent().Connect(
    mOwnerThread, this, &VideoSink::OnVideoQueueFinished);
}

void
VideoSink::DisconnectListener()
{
  AssertOwnerThread();
  mPushListener.Disconnect();
  mFinishListener.Disconnect();
}

void
VideoSink::RenderVideoFrames(int32_t aMaxFrames,
                             int64_t aClockTime,
                             const TimeStamp& aClockTimeStamp)
{
  AssertOwnerThread();

  AutoTArray<RefPtr<MediaData>,16> frames;
  VideoQueue().GetFirstElements(aMaxFrames, &frames);
  if (frames.IsEmpty() || !mContainer) {
    return;
  }

  AutoTArray<ImageContainer::NonOwningImage,16> images;
  TimeStamp lastFrameTime;
  MediaSink::PlaybackParams params = mAudioSink->GetPlaybackParams();
  for (uint32_t i = 0; i < frames.Length(); ++i) {
    VideoData* frame = frames[i]->As<VideoData>();

    frame->mSentToCompositor = true;

    if (!frame->mImage || !frame->mImage->IsValid()) {
      continue;
    }

    int64_t frameTime = frame->mTime;
    if (frameTime < 0) {
      // Frame times before the start time are invalid; drop such frames
      continue;
    }

    TimeStamp t;
    if (aMaxFrames > 1) {
      MOZ_ASSERT(!aClockTimeStamp.IsNull());
      int64_t delta = frame->mTime - aClockTime;
      t = aClockTimeStamp +
          TimeDuration::FromMicroseconds(delta / params.mPlaybackRate);
      if (!lastFrameTime.IsNull() && t <= lastFrameTime) {
        // Timestamps out of order; drop the new frame. In theory we should
        // probably replace the previous frame with the new frame if the
        // timestamps are equal, but this is a corrupt video file already so
        // never mind.
        continue;
      }
      lastFrameTime = t;
    }

    ImageContainer::NonOwningImage* img = images.AppendElement();
    img->mTimeStamp = t;
    img->mImage = frame->mImage;
    img->mFrameID = frame->mFrameID;
    img->mProducerID = mProducerID;

    VSINK_LOG_V("playing video frame %lld (id=%x) (vq-queued=%i)",
                frame->mTime, frame->mFrameID, VideoQueue().GetSize());
  }
  mContainer->SetCurrentFrames(frames[0]->As<VideoData>()->mDisplay, images);
}

void
VideoSink::UpdateRenderedVideoFrames()
{
  AssertOwnerThread();
  MOZ_ASSERT(mAudioSink->IsPlaying(), "should be called while playing.");

  TimeStamp nowTime;
  const int64_t clockTime = mAudioSink->GetPosition(&nowTime);
  // Skip frames up to the frame at the playback position, and figure out
  // the time remaining until it's time to display the next frame and drop
  // the current frame.
  NS_ASSERTION(clockTime >= 0, "Should have positive clock time.");

  int64_t remainingTime = -1;
  if (VideoQueue().GetSize() > 0) {
    RefPtr<MediaData> currentFrame = VideoQueue().PopFront();
    int32_t framesRemoved = 0;
    while (VideoQueue().GetSize() > 0) {
      RefPtr<MediaData> nextFrame = VideoQueue().PeekFront();
      if (nextFrame->mTime > clockTime) {
        remainingTime = nextFrame->mTime - clockTime;
        break;
      }
      ++framesRemoved;
      if (!currentFrame->As<VideoData>()->mSentToCompositor) {
        mFrameStats.NotifyDecodedFrames(0, 0, 1);
        VSINK_LOG_V("discarding video frame mTime=%lld clock_time=%lld",
                    currentFrame->mTime, clockTime);
      }
      currentFrame = VideoQueue().PopFront();
    }
    VideoQueue().PushFront(currentFrame);
    if (framesRemoved > 0) {
      mVideoFrameEndTime = currentFrame->GetEndTime();
      mFrameStats.NotifyPresentedFrame();
    }
  }

  // All frames are rendered, Let's resolve the promise.
  if (VideoQueue().IsFinished() &&
      VideoQueue().GetSize() <= 1 &&
      !mVideoSinkEndRequest.Exists()) {
    mEndPromiseHolder.ResolveIfExists(true, __func__);
  }

  RenderVideoFrames(mVideoQueueSendToCompositorSize, clockTime, nowTime);

  // No next fame to render. There is no need to schedule next render
  // loop. We will run render loops again upon incoming frames.
  if (remainingTime < 0) {
    return;
  }

  TimeStamp target = nowTime + TimeDuration::FromMicroseconds(
    remainingTime / mAudioSink->GetPlaybackParams().mPlaybackRate);

  RefPtr<VideoSink> self = this;
  mUpdateScheduler.Ensure(target, [self] () {
    self->UpdateRenderedVideoFramesByTimer();
  }, [self] () {
    self->UpdateRenderedVideoFramesByTimer();
  });
}

} // namespace media
} // namespace mozilla
