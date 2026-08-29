/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidDecoderModule.h"
#include "AndroidBridge.h"

#include "MediaCodecDataDecoder.h"
#include "RemoteDataDecoder.h"

#include "MediaInfo.h"
#include "VPXDecoder.h"

#include "MediaPrefs.h"
#include "OpusDecoder.h"
#include "VorbisDecoder.h"

#include "nsPromiseFlatString.h"
#include "nsIGfxInfo.h"

#include "prlog.h"

#include <jni.h>

#undef LOG
#define LOG(arg, ...) MOZ_LOG(sAndroidDecoderModuleLog, \
    mozilla::LogLevel::Debug, ("AndroidDecoderModule(%p)::%s: " arg, \
      this, __func__, ##__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::gl;
using namespace mozilla::java::sdk;
using media::TimeUnit;

namespace mozilla {

mozilla::LazyLogModule sAndroidDecoderModuleLog("AndroidDecoderModule");

static const char*
TranslateMimeType(const nsACString& aMimeType)
{
  if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP8)) {
    return "video/x-vnd.on2.vp8";
  } else if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP9)) {
    return "video/x-vnd.on2.vp9";
  }
  return PromiseFlatCString(aMimeType).get();
}

static bool
GetFeatureStatus(int32_t aFeature)
{
  nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
  int32_t status = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  nsCString discardFailureId;
  if (!gfxInfo || NS_FAILED(gfxInfo->GetFeatureStatus(aFeature, discardFailureId, &status))) {
    return false;
  }
  return status == nsIGfxInfo::FEATURE_STATUS_OK;
};

class VideoDataDecoder : public MediaCodecDataDecoder
{
public:
  VideoDataDecoder(const VideoInfo& aConfig,
                   MediaFormat::Param aFormat,
                   MediaDataDecoderCallback* aCallback,
                   layers::ImageContainer* aImageContainer)
    : MediaCodecDataDecoder(MediaData::Type::VIDEO_DATA, aConfig.mMimeType,
                            aFormat, aCallback)
    , mImageContainer(aImageContainer)
    , mConfig(aConfig)
  {

  }

  const char* GetDescriptionName() const override
  {
    return "android video decoder";
  }

  RefPtr<InitPromise> Init() override
  {
    mSurfaceTexture = AndroidSurfaceTexture::Create();
    if (!mSurfaceTexture) {
      NS_WARNING("Failed to create SurfaceTexture for video decode\n");
      return InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
    }

    if (NS_FAILED(InitDecoder(mSurfaceTexture->JavaSurface()))) {
      return InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
    }

    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }

  void Cleanup() override
  {
  }

  nsresult Input(MediaRawData* aSample) override
  {
    return MediaCodecDataDecoder::Input(aSample);
  }

  nsresult PostOutput(BufferInfo::Param aInfo, MediaFormat::Param aFormat,
                      const TimeUnit& aDuration) override
  {
    RefPtr<layers::Image> img =
      new SurfaceTextureImage(mSurfaceTexture.get(), mConfig.mDisplay,
                              gl::OriginPos::BottomLeft);

    nsresult rv;
    int32_t flags;
    NS_ENSURE_SUCCESS(rv = aInfo->Flags(&flags), rv);

    bool isSync = !!(flags & MediaCodec::BUFFER_FLAG_SYNC_FRAME);

    int32_t offset;
    NS_ENSURE_SUCCESS(rv = aInfo->Offset(&offset), rv);

    int64_t presentationTimeUs;
    NS_ENSURE_SUCCESS(rv = aInfo->PresentationTimeUs(&presentationTimeUs), rv);

    RefPtr<VideoData> v =
      VideoData::CreateFromImage(mConfig,
                                 offset,
                                 presentationTimeUs,
                                 aDuration.ToMicroseconds(),
                                 img,
                                 isSync,
                                 presentationTimeUs,
                                 gfx::IntRect(0, 0,
                                              mConfig.mDisplay.width,
                                              mConfig.mDisplay.height));
    INVOKE_CALLBACK(Output, v);
    return NS_OK;
  }

protected:
  layers::ImageContainer* mImageContainer;
  const VideoInfo& mConfig;
  RefPtr<AndroidSurfaceTexture> mSurfaceTexture;
};

class AudioDataDecoder : public MediaCodecDataDecoder
{
public:
  AudioDataDecoder(const AudioInfo& aConfig, MediaFormat::Param aFormat,
                   MediaDataDecoderCallback* aCallback)
    : MediaCodecDataDecoder(MediaData::Type::AUDIO_DATA, aConfig.mMimeType,
                            aFormat, aCallback)
  {
    JNIEnv* const env = jni::GetEnvForThread();

    jni::ByteBuffer::LocalRef buffer(env);
    NS_ENSURE_SUCCESS_VOID(aFormat->GetByteBuffer(NS_LITERAL_STRING("csd-0"),
                                                  &buffer));

    if (!buffer && aConfig.mCodecSpecificConfig->Length() >= 2) {
      buffer = jni::ByteBuffer::New(
          aConfig.mCodecSpecificConfig->Elements(),
          aConfig.mCodecSpecificConfig->Length());
      NS_ENSURE_SUCCESS_VOID(aFormat->SetByteBuffer(NS_LITERAL_STRING("csd-0"),
                                                    buffer));
    }
  }

  const char* GetDescriptionName() const override
  {
    return "android audio decoder";
  }

  nsresult Output(BufferInfo::Param aInfo, void* aBuffer,
                  MediaFormat::Param aFormat, const TimeUnit& aDuration) override
  {
    // The output on Android is always 16-bit signed
    nsresult rv;
    int32_t numChannels;
    NS_ENSURE_SUCCESS(rv =
        aFormat->GetInteger(NS_LITERAL_STRING("channel-count"), &numChannels), rv);
    AudioConfig::ChannelLayout layout(numChannels);
    if (!layout.IsValid()) {
      return NS_ERROR_FAILURE;
    }

    int32_t sampleRate;
    NS_ENSURE_SUCCESS(rv =
        aFormat->GetInteger(NS_LITERAL_STRING("sample-rate"), &sampleRate), rv);

    int32_t size;
    NS_ENSURE_SUCCESS(rv = aInfo->Size(&size), rv);

    int32_t offset;
    NS_ENSURE_SUCCESS(rv = aInfo->Offset(&offset), rv);

#ifdef MOZ_SAMPLE_TYPE_S16
    const int32_t numSamples = size / 2;
#else
#error We only support 16-bit integer PCM
#endif

    const int32_t numFrames = numSamples / numChannels;
    AlignedAudioBuffer audio(numSamples);
    if (!audio) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    const uint8_t* bufferStart = static_cast<uint8_t*>(aBuffer) + offset;
    PodCopy(audio.get(), reinterpret_cast<const AudioDataValue*>(bufferStart),
            numSamples);

    int64_t presentationTimeUs;
    NS_ENSURE_SUCCESS(rv = aInfo->PresentationTimeUs(&presentationTimeUs), rv);

    RefPtr<AudioData> data = new AudioData(0, presentationTimeUs,
                                           aDuration.ToMicroseconds(),
                                           numFrames,
                                           Move(audio),
                                           numChannels,
                                           sampleRate);
    INVOKE_CALLBACK(Output, data);
    return NS_OK;
  }
};

AndroidDecoderModule::AndroidDecoderModule(CDMProxy* aProxy)
{
  mProxy = static_cast<MediaDrmCDMProxy*>(aProxy);
}

bool
AndroidDecoderModule::SupportsMimeType(const nsACString& aMimeType,
                                       DecoderDoctorDiagnostics* aDiagnostics) const
{
  if (!AndroidBridge::Bridge() ||
      AndroidBridge::Bridge()->GetAPIVersion() < 16) {
    return false;
  }

  if (aMimeType.EqualsLiteral("video/mp4") ||
      aMimeType.EqualsLiteral("video/avc")) {
    return true;
  }

  // When checking "audio/x-wav", CreateDecoder can cause a JNI ERROR by
  // Accessing a stale local reference leading to a SIGSEGV crash.
  // To avoid this we check for wav types here.
  if (aMimeType.EqualsLiteral("audio/x-wav")
      || aMimeType.EqualsLiteral("audio/wave; codecs=1")
      || aMimeType.EqualsLiteral("audio/wave; codecs=6")
      || aMimeType.EqualsLiteral("audio/wave; codecs=7")
      || aMimeType.EqualsLiteral("audio/wave; codecs=65534")) {
    return false;
  }

  if ((VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP8)
       && !GetFeatureStatus(nsIGfxInfo::FEATURE_VP8_HW_DECODE))
      || (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP9)
          && !GetFeatureStatus(nsIGfxInfo::FEATURE_VP9_HW_DECODE))) {
    return false;
  }

  // Prefer the gecko decoder for opus and vorbis; stagefright crashes
  // on content demuxed from mp4.
  if (OpusDataDecoder::IsOpus(aMimeType)
      || VorbisDataDecoder::IsVorbis(aMimeType)) {
    LOG("Rejecting audio of type %s", aMimeType.Data());
    return false;
  }

  return java::HardwareCodecCapabilityUtils::FindDecoderCodecInfoForMimeType(
    nsCString(TranslateMimeType(aMimeType)));
}

already_AddRefed<MediaDataDecoder>
AndroidDecoderModule::CreateVideoDecoder(const CreateDecoderParams& aParams)
{
  // Temporary - forces use of VPXDecoder when alpha is present.
  // Bug 1263836 will handle alpha scenario once implemented. It will shift
  // the check for alpha to PDMFactory but not itself remove the need for a
  // check.
  if (aParams.VideoConfig().HasAlpha()) {
    return nullptr;
  }
  MediaFormat::LocalRef format;

  const VideoInfo& config = aParams.VideoConfig();
  NS_ENSURE_SUCCESS(MediaFormat::CreateVideoFormat(
      TranslateMimeType(config.mMimeType),
      config.mDisplay.width,
      config.mDisplay.height,
      &format), nullptr);

  RefPtr<MediaDataDecoder> decoder = MediaPrefs::PDMAndroidRemoteCodecEnabled() ?
      RemoteDataDecoder::CreateVideoDecoder(config,
                                            format,
                                            aParams.mCallback,
                                            aParams.mImageContainer) :
      MediaCodecDataDecoder::CreateVideoDecoder(config,
                                                format,
                                                aParams.mCallback,
                                                aParams.mImageContainer);

  return decoder.forget();
}

already_AddRefed<MediaDataDecoder>
AndroidDecoderModule::CreateAudioDecoder(const CreateDecoderParams& aParams)
{
  const AudioInfo& config = aParams.AudioConfig();
  if (config.mBitDepth != 16) {
    // We only handle 16-bit audio.
    return nullptr;
  }

  MediaFormat::LocalRef format;

  LOG("CreateAudioFormat with mimeType=%s, mRate=%d, channels=%d",
      config.mMimeType.Data(), config.mRate, config.mChannels);

  NS_ENSURE_SUCCESS(MediaFormat::CreateAudioFormat(
      config.mMimeType,
      config.mRate,
      config.mChannels,
      &format), nullptr);

  RefPtr<MediaDataDecoder> decoder = MediaPrefs::PDMAndroidRemoteCodecEnabled() ?
      RemoteDataDecoder::CreateAudioDecoder(config, format, aParams.mCallback) :
      MediaCodecDataDecoder::CreateAudioDecoder(config, format, aParams.mCallback);

  return decoder.forget();
}

PlatformDecoderModule::ConversionRequired
AndroidDecoderModule::DecoderNeedsConversion(const TrackInfo& aConfig) const
{
  if (aConfig.IsVideo()) {
    return ConversionRequired::kNeedAnnexB;
  }
  return ConversionRequired::kNeedNone;
}

} // mozilla
