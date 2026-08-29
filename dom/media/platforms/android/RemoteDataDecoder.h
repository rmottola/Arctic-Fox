/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef RemoteDataDecoder_h_
#define RemoteDataDecoder_h_

#include "AndroidDecoderModule.h"

#include "GeneratedJNIWrappers.h"

#include "SurfaceTexture.h"
#include "TimeUnits.h"
#include "mozilla/Monitor.h"
#include "mozilla/Maybe.h"

#include <deque>

namespace mozilla {

class RemoteDataDecoder : public MediaDataDecoder
{
public:
  static already_AddRefed<MediaDataDecoder>
  CreateAudioDecoder(const CreateDecoderParams& aParams,
                     const nsString& aDrmStubId,
                     CDMProxy* aProxy);

  static already_AddRefed<MediaDataDecoder>
  CreateVideoDecoder(const CreateDecoderParams& aParams,
                     const nsString& aDrmStubId,
                     CDMProxy* aProxy);

  virtual ~RemoteDataDecoder() {}

  nsresult Flush() override;
  nsresult Drain() override;
  nsresult Shutdown() override;
  nsresult Input(MediaRawData* aSample) override;
  const char* GetDescriptionName() const override
  {
    return "android remote decoder";
  }

protected:
  virtual ~RemoteDataDecoder() { }
  RemoteDataDecoder(MediaData::Type aType,
                    const nsACString& aMimeType,
                    java::sdk::MediaFormat::Param aFormat,
                    MediaDataDecoderCallback* aCallback);

  MediaData::Type mType;

  nsAutoCString mMimeType;
  java::sdk::MediaFormat::GlobalRef mFormat;

  MediaDataDecoderCallback* mCallback;

  java::CodecProxy::GlobalRef mJavaDecoder;
  java::CodecProxy::NativeCallbacks::GlobalRef mJavaCallbacks;
};

} // namespace mozilla

#endif
