/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPDecoderModule.h"
#include "DecoderDoctorDiagnostics.h"
#include "GMPAudioDecoder.h"
#include "GMPVideoDecoder.h"
#include "MediaDataDecoderProxy.h"
#include "MediaPrefs.h"
#include "mozIGeckoMediaPluginService.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/StaticMutex.h"
#include "gmp-audio-decode.h"
#include "gmp-video-decode.h"
#include "MP4Decoder.h"
#include "VPXDecoder.h"
#ifdef XP_WIN
#include "WMFDecoderModule.h"
#endif

namespace mozilla {

GMPDecoderModule::GMPDecoderModule()
{
}

GMPDecoderModule::~GMPDecoderModule()
{
}

static already_AddRefed<MediaDataDecoderProxy>
CreateDecoderWrapper(MediaDataDecoderCallback* aCallback)
{
  RefPtr<gmp::GeckoMediaPluginService> s(gmp::GeckoMediaPluginService::GetGeckoMediaPluginService());
  if (!s) {
    return nullptr;
  }
  RefPtr<AbstractThread> thread(s->GetAbstractGMPThread());
  if (!thread) {
    return nullptr;
  }
  RefPtr<MediaDataDecoderProxy> decoder(new MediaDataDecoderProxy(thread.forget(), aCallback));
  return decoder.forget();
}

already_AddRefed<MediaDataDecoder>
GMPDecoderModule::CreateVideoDecoder(const CreateDecoderParams& aParams)
{
  if (!MP4Decoder::IsH264(aParams.mConfig.mMimeType) &&
      !VPXDecoder::IsVP8(aParams.mConfig.mMimeType) &&
      !VPXDecoder::IsVP9(aParams.mConfig.mMimeType)) {
    return nullptr;
  }

  if (aParams.mDiagnostics) {
    const Maybe<nsCString> preferredGMP = PreferredGMP(aParams.mConfig.mMimeType);
    if (preferredGMP.isSome()) {
      aParams.mDiagnostics->SetGMP(preferredGMP.value());
    }
  }

  RefPtr<MediaDataDecoderProxy> wrapper = CreateDecoderWrapper(aParams.mCallback);
  auto params = GMPVideoDecoderParams(aParams).WithCallback(wrapper);
  wrapper->SetProxyTarget(new GMPVideoDecoder(params));
  return wrapper.forget();
}

already_AddRefed<MediaDataDecoder>
GMPDecoderModule::CreateAudioDecoder(const CreateDecoderParams& aParams)
{
  if (!aParams.mConfig.mMimeType.EqualsLiteral("audio/mp4a-latm")) {
    return nullptr;
  }

  if (aParams.mDiagnostics) {
    const Maybe<nsCString> preferredGMP = PreferredGMP(aParams.mConfig.mMimeType);
    if (preferredGMP.isSome()) {
      aParams.mDiagnostics->SetGMP(preferredGMP.value());
    }
  }

  RefPtr<MediaDataDecoderProxy> wrapper = CreateDecoderWrapper(aParams.mCallback);
  auto params = GMPAudioDecoderParams(aParams).WithCallback(wrapper);
  wrapper->SetProxyTarget(new GMPAudioDecoder(params));
  return wrapper.forget();
}

PlatformDecoderModule::ConversionRequired
GMPDecoderModule::DecoderNeedsConversion(const TrackInfo& aConfig) const
{
  // GMPVideoCodecType::kGMPVideoCodecH264 specifies that encoded frames must be in AVCC format.
  if (aConfig.IsVideo() && MP4Decoder::IsH264(aConfig.mMimeType)) {
    return kNeedAVCC;
  } else {
    return kNeedNone;
  }
}

static bool
HasGMPFor(const nsACString& aAPI,
          const nsACString& aCodec,
          const nsACString& aGMP)
{
#ifdef XP_WIN
  // gmp-clearkey uses WMF for decoding, so if we're using clearkey we must
  // verify that WMF works before continuing.
  if (aGMP.Equals(kEMEKeySystemClearkey)) {
    RefPtr<WMFDecoderModule> pdm(new WMFDecoderModule());
    if (aCodec.EqualsLiteral("aac") &&
        !pdm->SupportsMimeType(NS_LITERAL_CSTRING("audio/mp4a-latm"),
                               /* DecoderDoctorDiagnostics* */ nullptr)) {
      return false;
    }
    if (aCodec.EqualsLiteral("h264") &&
        !pdm->SupportsMimeType(NS_LITERAL_CSTRING("video/avc"),
                               /* DecoderDoctorDiagnostics* */ nullptr)) {
      return false;
    }
  }
#endif
  MOZ_ASSERT(NS_IsMainThread(),
             "HasPluginForAPI must be called on the main thread");
  nsTArray<nsCString> tags;
  tags.AppendElement(aCodec);
  tags.AppendElement(aGMP);
  nsCOMPtr<mozIGeckoMediaPluginService> mps =
    do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  if (NS_WARN_IF(!mps)) {
    return false;
  }
  bool hasPlugin = false;
  if (NS_FAILED(mps->HasPluginForAPI(aAPI, &tags, &hasPlugin))) {
    return false;
  }
  return hasPlugin;
}

StaticMutex sGMPCodecsMutex;

struct GMPCodecs {
  const char* mKeySystem;
  bool mHasAAC;
  bool mHasH264;
  bool mHasVP8;
  bool mHasVP9;
};

static GMPCodecs sGMPCodecs[] = {
  { kEMEKeySystemClearkey, false, false, false, false },
  { kEMEKeySystemWidevine, false, false, false, false },
  { kEMEKeySystemPrimetime, false, false, false, false },
};

void
GMPDecoderModule::UpdateUsableCodecs()
{
  MOZ_ASSERT(NS_IsMainThread());

  StaticMutexAutoLock lock(sGMPCodecsMutex);
  for (GMPCodecs& gmp : sGMPCodecs) {
    gmp.mHasAAC = HasGMPFor(NS_LITERAL_CSTRING(GMP_API_AUDIO_DECODER),
                            NS_LITERAL_CSTRING("aac"),
                            nsDependentCString(gmp.mKeySystem));
    gmp.mHasH264 = HasGMPFor(NS_LITERAL_CSTRING(GMP_API_VIDEO_DECODER),
                             NS_LITERAL_CSTRING("h264"),
                             nsDependentCString(gmp.mKeySystem));
    gmp.mHasVP8 = HasGMPFor(NS_LITERAL_CSTRING(GMP_API_VIDEO_DECODER),
                             NS_LITERAL_CSTRING("vp8"),
                             nsDependentCString(gmp.mKeySystem));
    gmp.mHasVP9 = HasGMPFor(NS_LITERAL_CSTRING(GMP_API_VIDEO_DECODER),
                             NS_LITERAL_CSTRING("vp9"),
                             nsDependentCString(gmp.mKeySystem));
  }
}

/* static */
void
GMPDecoderModule::Init()
{
  MOZ_ASSERT(NS_IsMainThread());
  // GMPService::HasPluginForAPI is main thread only, so to implement
  // SupportsMimeType() we build a table of the codecs which each whitelisted
  // GMP has and update it when any GMPs are removed or added at runtime.
  UpdateUsableCodecs();
}

/* static */
const Maybe<nsCString>
GMPDecoderModule::PreferredGMP(const nsACString& aMimeType)
{
  Maybe<nsCString> rv;
  if (aMimeType.EqualsLiteral("audio/mp4a-latm")) {
    switch (MediaPrefs::GMPAACPreferred()) {
      case 1: rv.emplace(nsCString(kEMEKeySystemClearkey)); break;
      case 2: rv.emplace(nsCString(kEMEKeySystemPrimetime)); break;
      default: break;
    }
  }

  if (MP4Decoder::IsH264(aMimeType)) {
    switch (MediaPrefs::GMPH264Preferred()) {
      case 1: rv.emplace(nsCString(kEMEKeySystemClearkey)); break;
      case 2: rv.emplace(nsCString(kEMEKeySystemPrimetime)); break;
      default: break;
    }
  }

  return rv;
}

/* static */
bool
GMPDecoderModule::SupportsMimeType(const nsACString& aMimeType,
                                   const Maybe<nsCString>& aGMP)
{
  StaticMutexAutoLock lock(sGMPCodecsMutex);
  for (GMPCodecs& gmp : sGMPCodecs) {
    if (((aMimeType.EqualsLiteral("audio/mp4a-latm") && gmp.mHasAAC) ||
         (MP4Decoder::IsH264(aMimeType) && gmp.mHasH264) ||
         (VPXDecoder::IsVP8(aMimeType) && gmp.mHasVP8) ||
         (VPXDecoder::IsVP9(aMimeType) && gmp.mHasVP9)) &&
        (aGMP.isNothing() || aGMP.value().EqualsASCII(gmp.mKeySystem))) {
      return true;
    }
  }

  return false;
}

bool
GMPDecoderModule::SupportsMimeType(const nsACString& aMimeType,
                                   DecoderDoctorDiagnostics* aDiagnostics) const
{
  const Maybe<nsCString> preferredGMP = PreferredGMP(aMimeType);
  bool rv = SupportsMimeType(aMimeType, preferredGMP);
  if (rv && aDiagnostics && preferredGMP.isSome()) {
    aDiagnostics->SetGMP(preferredGMP.value());
  }
  return rv;
}

} // namespace mozilla
