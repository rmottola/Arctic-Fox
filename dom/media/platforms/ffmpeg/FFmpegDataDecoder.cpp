/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/TaskQueue.h"

#include <string.h>
#include <unistd.h>

#include "FFmpegLog.h"
#include "FFmpegDataDecoder.h"
#include "prsystem.h"
#include "FFmpegRuntimeLinker.h"

namespace mozilla
{

bool FFmpegDataDecoder<LIBAV_VER>::sFFmpegInitDone = false;
StaticMutex FFmpegDataDecoder<LIBAV_VER>::sMonitor;

FFmpegDataDecoder<LIBAV_VER>::FFmpegDataDecoder(FlushableTaskQueue* aTaskQueue,
                                                AVCodecID aCodecID)
  : mTaskQueue(aTaskQueue)
  , mCodecContext(nullptr)
  , mFrame(NULL)
  , mExtraData(nullptr)
  , mCodecID(aCodecID)
{
  MOZ_COUNT_CTOR(FFmpegDataDecoder);
}

FFmpegDataDecoder<LIBAV_VER>::~FFmpegDataDecoder()
{
  MOZ_COUNT_DTOR(FFmpegDataDecoder);
}

nsresult
FFmpegDataDecoder<LIBAV_VER>::InitDecoder()
{
  FFMPEG_LOG("Initialising FFmpeg decoder.");

  AVCodec* codec = FindAVCodec(mCodecID);
  if (!codec) {
    NS_WARNING("Couldn't find ffmpeg decoder");
    return NS_ERROR_FAILURE;
  }

  StaticMutexAutoLock mon(sMonitor);

  if (!(mCodecContext = avcodec_alloc_context3(codec))) {
    NS_WARNING("Couldn't init ffmpeg context");
    return NS_ERROR_FAILURE;
  }

  mCodecContext->opaque = this;

  InitCodecContext();

  if (mExtraData) {
    mCodecContext->extradata_size = mExtraData->Length();
    // FFmpeg may use SIMD instructions to access the data which reads the
    // data in 32 bytes block. Must ensure we have enough data to read.
#if LIBAVCODEC_VERSION_MAJOR >= 58
    mExtraData->AppendElements(AV_INPUT_BUFFER_PADDING_SIZE);
#else
    mExtraData->AppendElements(FF_INPUT_BUFFER_PADDING_SIZE);
#endif
    mCodecContext->extradata = mExtraData->Elements();
  } else {
    mCodecContext->extradata_size = 0;
  }

#if LIBAVCODEC_VERSION_MAJOR < 57
  if (codec->capabilities & CODEC_CAP_DR1) {
    mCodecContext->flags |= CODEC_FLAG_EMU_EDGE;
  }
#endif

  if (avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    NS_WARNING("Couldn't initialise ffmpeg decoder");
    avcodec_close(mCodecContext);
    av_freep(&mCodecContext);
    return NS_ERROR_FAILURE;
  }

  if (mCodecContext->codec_type == AVMEDIA_TYPE_AUDIO &&
      mCodecContext->sample_fmt != AV_SAMPLE_FMT_FLT &&
      mCodecContext->sample_fmt != AV_SAMPLE_FMT_FLTP &&
      mCodecContext->sample_fmt != AV_SAMPLE_FMT_S16 &&
      mCodecContext->sample_fmt != AV_SAMPLE_FMT_S16P) {
    NS_WARNING("FFmpeg audio decoder outputs unsupported audio format.");
    return NS_ERROR_FAILURE;
  }

  FFMPEG_LOG("FFmpeg init successful.");
  return NS_OK;
}

nsresult
FFmpegDataDecoder<LIBAV_VER>::Flush()
{
  mTaskQueue->Flush();
  if (mCodecContext) {
    avcodec_flush_buffers(mCodecContext);
  }
  return NS_OK;
}

nsresult
FFmpegDataDecoder<LIBAV_VER>::Shutdown()
{
  StaticMutexAutoLock mon(sMonitor);

  if (sFFmpegInitDone && mCodecContext) {
    avcodec_close(mCodecContext);
    av_freep(&mCodecContext);
#if LIBAVCODEC_VERSION_MAJOR >= 55
    av_frame_free(&mFrame);
#elif LIBAVCODEC_VERSION_MAJOR == 54
    avcodec_free_frame(&mFrame);
#else
    av_freep(&mFrame);
#endif
  }
  return NS_OK;
}

AVFrame*
FFmpegDataDecoder<LIBAV_VER>::PrepareFrame()
{
#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (mFrame) {
    av_frame_unref(mFrame);
  } else {
    mFrame = av_frame_alloc();
  }
#elif LIBAVCODEC_VERSION_MAJOR == 54
  if (mFrame) {
    avcodec_get_frame_defaults(mFrame);
  } else {
    mFrame = avcodec_alloc_frame();
  }
#else
  av_freep(&mFrame);
  mFrame = avcodec_alloc_frame();
#endif
  return mFrame;
}

/* static */ AVCodec*
FFmpegDataDecoder<LIBAV_VER>::FindAVCodec(AVCodecID aCodec)
{
  StaticMutexAutoLock mon(sMonitor);
  if (!sFFmpegInitDone) {
    avcodec_register_all();
#ifdef DEBUG
    av_log_set_level(AV_LOG_DEBUG);
#endif
    sFFmpegInitDone = true;
  }
  return avcodec_find_decoder(aCodec);
}

} // namespace mozilla
