/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __FFmpegDataDecoder_h__
#define __FFmpegDataDecoder_h__

#include "PlatformDecoderModule.h"
#include "FFmpegLibWrapper.h"
#include "mozilla/StaticMutex.h"
#include "FFmpegLibs.h"

namespace mozilla
{

template <int V>
class FFmpegDataDecoder : public MediaDataDecoder
{
};

template <>
class FFmpegDataDecoder<LIBAV_VER> : public MediaDataDecoder
{
public:
  FFmpegDataDecoder(FFmpegLibWrapper* aLib, FlushableTaskQueue* aTaskQueue,
                    MediaDataDecoderCallback* aCallback,
                    AVCodecID aCodecID);
  virtual ~FFmpegDataDecoder();

  static bool Link();

  RefPtr<InitPromise> Init() override = 0;
  nsresult Input(MediaRawData* aSample) override = 0;
  nsresult Flush() override;
  nsresult Drain() override;
  nsresult Shutdown() override;

  static AVCodec* FindAVCodec(FFmpegLibWrapper* aLib, AVCodecID aCodec);

protected:
  // Flush and Drain operation, always run
  virtual void ProcessFlush();
  virtual void ProcessDrain() = 0;
  virtual void ProcessShutdown();
  virtual void InitCodecContext() {}
  AVFrame*        PrepareFrame();
  nsresult        InitDecoder();

  FFmpegLibWrapper* mLib;
  RefPtr<FlushableTaskQueue> mTaskQueue;
  MediaDataDecoderCallback* mCallback;

  AVCodecContext* mCodecContext;
  AVFrame*        mFrame;
  RefPtr<MediaByteBuffer> mExtraData;
  AVCodecID mCodecID;

private:
  static StaticMutex sMonitor;
};

} // namespace mozilla

#endif // __FFmpegDataDecoder_h__
