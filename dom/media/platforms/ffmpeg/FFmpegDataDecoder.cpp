/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#if defined(XP_WIN)
#else
#include <unistd.h>
#endif

#include "MediaTaskQueue.h"
#include "FFmpegLog.h"
#include "FFmpegDataDecoder.h"
#include "prsystem.h"
#include "FFmpegRuntimeLinker.h"

namespace mozilla
{

bool FFmpegDataDecoder<LIBAV_VER>::sFFmpegInitDone = false;
StaticMutex FFmpegDataDecoder<LIBAV_VER>::sMonitor;

FFmpegDataDecoder<LIBAV_VER>::FFmpegDataDecoder(FlushableMediaTaskQueue* aTaskQueue,
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
FFmpegDataDecoder<LIBAV_VER>::Init()
{
  FFMPEG_LOG("Initialising FFmpeg decoder.");

  AVCodec* codec = FindAVCodec(mCodecID);
  if (!codec) {
    NS_WARNING("Couldn't find ffmpeg decoder");
    return NS_ERROR_FAILURE;
  }

  StaticMutexAutoLock mon(sMonitor);

  if (!(mCodecContext = 
#if defined(XP_WIN)
       reinterpret_cast<AVCodecContext*(*)(const AVCodec*)>(FFmpegRuntimeLinker::avc_ptr[_alloc_context3])(codec)
#else
       avcodec_alloc_context3(codec)
#endif
      )) {
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

  if (
#if defined(XP_WIN)
      reinterpret_cast<int(*)(AVCodecContext*,const AVCodec*,AVDictionary**)>(FFmpegRuntimeLinker::avc_ptr[_open2])(mCodecContext, codec, nullptr)
#else
      avcodec_open2(mCodecContext, codec, nullptr)
#endif
     < 0) {
    NS_WARNING("Couldn't initialise ffmpeg decoder");
#if defined(XP_WIN)
    reinterpret_cast<int(*)(AVCodecContext*)>(FFmpegRuntimeLinker::avc_ptr[_close])(mCodecContext);
    reinterpret_cast<void(*)(void*)>(FFmpegRuntimeLinker::avc_ptr[_freep])(&mCodecContext);
#else
    avcodec_close(mCodecContext);
    av_freep(&mCodecContext);
#endif
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
#if defined(XP_WIN)
    reinterpret_cast<void(*)(AVCodecContext*)>(FFmpegRuntimeLinker::avc_ptr[_flush_buffers])(mCodecContext);
#else
    avcodec_flush_buffers(mCodecContext);
#endif
  }
  return NS_OK;
}

nsresult
FFmpegDataDecoder<LIBAV_VER>::Shutdown()
{
  StaticMutexAutoLock mon(sMonitor);

  if (sFFmpegInitDone && mCodecContext) {
#if defined(XP_WIN)
    reinterpret_cast<int(*)(AVCodecContext*)>(FFmpegRuntimeLinker::avc_ptr[_close])(mCodecContext);
    reinterpret_cast<void(*)(void*)>(FFmpegRuntimeLinker::avc_ptr[_freep])(&mCodecContext);
    reinterpret_cast<void(*)(AVFrame**)>(FFmpegRuntimeLinker::avc_ptr[_frame_free])(&mFrame);
#else
    avcodec_close(mCodecContext);
    av_freep(&mCodecContext);
#if LIBAVCODEC_VERSION_MAJOR >= 55
    av_frame_free(&mFrame);
#elif LIBAVCODEC_VERSION_MAJOR == 54
    avcodec_free_frame(&mFrame);
#else
    av_freep(&mFrame);
#endif
#endif
  }
  return NS_OK;
}

AVFrame*
FFmpegDataDecoder<LIBAV_VER>::PrepareFrame()
{
#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (mFrame) {
#if defined(XP_WIN)
    reinterpret_cast<void(*)(AVFrame*)>(FFmpegRuntimeLinker::avc_ptr[_frame_unref])(mFrame);
#else
    av_frame_unref(mFrame);
#endif
  } else {
    mFrame = 
#if defined(XP_WIN)
        reinterpret_cast<AVFrame*(*)()>(FFmpegRuntimeLinker::avc_ptr[_frame_alloc])();
#else
        av_frame_alloc();
#endif
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
#if defined(XP_WIN)
    reinterpret_cast<void(*)()>(FFmpegRuntimeLinker::avc_ptr[_register_all])();
#else
    avcodec_register_all();
#endif
#ifdef DEBUG
#if defined(XP_WIN)
    reinterpret_cast<void(*)(int)>(FFmpegRuntimeLinker::avc_ptr[_log_set_level])(AV_LOG_DEBUG);
#else
    av_log_set_level(AV_LOG_DEBUG);
#endif
#endif
    sFFmpegInitDone = true;
  }
#if defined(XP_WIN)
  return reinterpret_cast<AVCodec*(*)(enum AVCodecID)>(FFmpegRuntimeLinker::avc_ptr[_find_decoder])(aCodec);
#else
  return avcodec_find_decoder(aCodec);
#endif
}

} // namespace mozilla
