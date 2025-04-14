/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PDMFactory.h"

#ifdef XP_WIN
#include "WMFDecoderModule.h"
#endif
#ifdef MOZ_FFVPX
#include "FFVPXRuntimeLinker.h"
#endif
#ifdef MOZ_FFMPEG
#include "FFmpegRuntimeLinker.h"
#endif
#ifdef MOZ_APPLEMEDIA
#include "AppleDecoderModule.h"
#endif
#ifdef MOZ_GONK_MEDIACODEC
#include "GonkDecoderModule.h"
#endif
#ifdef MOZ_WIDGET_ANDROID
#include "AndroidDecoderModule.h"
#endif
#include "GMPDecoderModule.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskQueue.h"

#include "MediaInfo.h"
#include "MediaPrefs.h"
#include "FuzzingWrapper.h"
#include "H264Converter.h"

#include "AgnosticDecoderModule.h"

#ifdef MOZ_EME
#include "EMEDecoderModule.h"
#include "mozilla/CDMProxy.h"
#endif

#include "DecoderDoctorDiagnostics.h"


namespace mozilla {

extern already_AddRefed<PlatformDecoderModule> CreateAgnosticDecoderModule();
extern already_AddRefed<PlatformDecoderModule> CreateBlankDecoderModule();

class PDMFactoryImpl final {
public:
  PDMFactoryImpl()
  {
#ifdef XP_WIN
    WMFDecoderModule::Init();
#endif
#ifdef MOZ_APPLEMEDIA
    AppleDecoderModule::Init();
#endif
#ifdef MOZ_FFVPX
    FFVPXRuntimeLinker::Init();
#endif
#ifdef MOZ_FFMPEG
    FFmpegRuntimeLinker::Init();
#endif
    GMPDecoderModule::Init();
  }
};

StaticAutoPtr<PDMFactoryImpl> PDMFactory::sInstance;
StaticMutex PDMFactory::sMonitor;

PDMFactory::PDMFactory()
{
  EnsureInit();
  CreatePDMs();
}

PDMFactory::~PDMFactory()
{
}

void
PDMFactory::EnsureInit() const
{
  StaticMutexAutoLock mon(sMonitor);
  if (!sInstance) {
    sInstance = new PDMFactoryImpl();
    if (NS_IsMainThread()) {
      ClearOnShutdown(&sInstance);
    } else {
      nsCOMPtr<nsIRunnable> runnable =
        NS_NewRunnableFunction([]() { ClearOnShutdown(&sInstance); });
      NS_DispatchToMainThread(runnable);
    }
  }
}

already_AddRefed<MediaDataDecoder>
PDMFactory::CreateDecoder(const CreateDecoderParams& aParams)
{
  const TrackInfo& config = aParams.mConfig;
  bool isEncrypted = mEMEPDM && config.mCrypto.mValid;

  if (isEncrypted) {
    return CreateDecoderWithPDM(mEMEPDM, aParams);
  }

  DecoderDoctorDiagnostics* diagnostics = aParams.mDiagnostics;
  if (diagnostics) {
    // If libraries failed to load, the following loop over mCurrentPDMs
    // will not even try to use them. So we record failures now.
    if (mWMFFailedToLoad) {
      diagnostics->SetWMFFailedToLoad();
    }
    if (mFFmpegFailedToLoad) {
      diagnostics->SetFFmpegFailedToLoad();
    }
    if (mGMPPDMFailedToStartup) {
      diagnostics->SetGMPPDMFailedToStartup();
    }
  }

  for (auto& current : mCurrentPDMs) {
    if (!current->SupportsMimeType(config.mMimeType, diagnostics)) {
      continue;
    }
    RefPtr<MediaDataDecoder> m = CreateDecoderWithPDM(current, aParams);
    if (m) {
      return m.forget();
    }
  }
  NS_WARNING("Unable to create a decoder, no platform found.");
  return nullptr;
}

already_AddRefed<MediaDataDecoder>
PDMFactory::CreateDecoderWithPDM(PlatformDecoderModule* aPDM,
                                 const CreateDecoderParams& aParams)
{
  MOZ_ASSERT(aPDM);
  RefPtr<MediaDataDecoder> m;

  const TrackInfo& config = aParams.mConfig;
  if (config.IsAudio()) {
    m = aPDM->CreateAudioDecoder(aParams);
    return m.forget();
  }

  if (!config.IsVideo()) {
    return nullptr;
  }

  MediaDataDecoderCallback* callback = aParams.mCallback;
  RefPtr<DecoderCallbackFuzzingWrapper> callbackWrapper;
  if (MediaPrefs::PDMFuzzingEnabled()) {
    callbackWrapper = new DecoderCallbackFuzzingWrapper(callback);
    callbackWrapper->SetVideoOutputMinimumInterval(
      TimeDuration::FromMilliseconds(MediaPrefs::PDMFuzzingInterval()));
    callbackWrapper->SetDontDelayInputExhausted(!MediaPrefs::PDMFuzzingDelayInputExhausted());
    callback = callbackWrapper.get();
  }

  CreateDecoderParams params = aParams;
  params.mCallback = callback;

  if (H264Converter::IsH264(config)) {
    RefPtr<H264Converter> h = new H264Converter(aPDM, params);
    const nsresult rv = h->GetLastError();
    if (NS_SUCCEEDED(rv) || rv == NS_ERROR_NOT_INITIALIZED) {
      // The H264Converter either successfully created the wrapped decoder,
      // or there wasn't enough AVCC data to do so. Otherwise, there was some
      // problem, for example WMF DLLs were missing.
      m = h.forget();
    }
  } else {
    m = aPDM->CreateVideoDecoder(params);
  }

  if (callbackWrapper && m) {
    m = new DecoderFuzzingWrapper(m.forget(), callbackWrapper.forget());
  }

  return m.forget();
}

bool
PDMFactory::SupportsMimeType(const nsACString& aMimeType,
                             DecoderDoctorDiagnostics* aDiagnostics) const
{
  if (mEMEPDM) {
    return mEMEPDM->SupportsMimeType(aMimeType, aDiagnostics);
  }
  RefPtr<PlatformDecoderModule> current = GetDecoder(aMimeType, aDiagnostics);
  return !!current;
}

void
PDMFactory::CreatePDMs()
{
  RefPtr<PlatformDecoderModule> m;

  if (MediaPrefs::PDMUseBlankDecoder()) {
    m = CreateBlankDecoderModule();
    StartupPDM(m);
    // The Blank PDM SupportsMimeType reports true for all codecs; the creation
    // of its decoder is infallible. As such it will be used for all media, we
    // can stop creating more PDM from this point.
    return;
  }

#ifdef MOZ_WIDGET_ANDROID
  if(MediaPrefs::PDMAndroidMediaCodecPreferred() &&
     MediaPrefs::PDMAndroidMediaCodecEnabled()) {
    m = new AndroidDecoderModule();
    StartupPDM(m);
  }
#endif
#ifdef XP_WIN
  if (MediaPrefs::PDMWMFEnabled()) {
    m = new WMFDecoderModule();
    mWMFFailedToLoad = !StartupPDM(m);
  } else {
    mWMFFailedToLoad = MediaPrefs::DecoderDoctorWMFDisabledIsFailure();
  }
#endif
#ifdef MOZ_FFVPX
  if (MediaPrefs::PDMFFVPXEnabled()) {
    m = FFVPXRuntimeLinker::CreateDecoderModule();
    StartupPDM(m);
  }
#endif
#ifdef MOZ_FFMPEG
  if (MediaPrefs::PDMFFmpegEnabled()) {
    m = FFmpegRuntimeLinker::CreateDecoderModule();
    mFFmpegFailedToLoad = !StartupPDM(m);
  } else {
    mFFmpegFailedToLoad = false;
  }
#endif
#ifdef MOZ_APPLEMEDIA
  m = new AppleDecoderModule();
  StartupPDM(m);
#endif
#ifdef MOZ_GONK_MEDIACODEC
  if (MediaPrefs::PDMGonkDecoderEnabled()) {
    m = new GonkDecoderModule();
    StartupPDM(m);
  }
#endif
#ifdef MOZ_WIDGET_ANDROID
  if(MediaPrefs::PDMAndroidMediaCodecEnabled()){
    m = new AndroidDecoderModule();
    StartupPDM(m);
  }
#endif

  m = new AgnosticDecoderModule();
  StartupPDM(m);

  if (MediaPrefs::PDMGMPEnabled()) {
    m = new GMPDecoderModule();
    mGMPPDMFailedToStartup = !StartupPDM(m);
  } else {
    mGMPPDMFailedToStartup = false;
  }
}

bool
PDMFactory::StartupPDM(PlatformDecoderModule* aPDM)
{
  if (aPDM && NS_SUCCEEDED(aPDM->Startup())) {
    mCurrentPDMs.AppendElement(aPDM);
    return true;
  }
  return false;
}

already_AddRefed<PlatformDecoderModule>
PDMFactory::GetDecoder(const nsACString& aMimeType,
                       DecoderDoctorDiagnostics* aDiagnostics) const
{
  if (aDiagnostics) {
    // If libraries failed to load, the following loop over mCurrentPDMs
    // will not even try to use them. So we record failures now.
    if (mWMFFailedToLoad) {
      aDiagnostics->SetWMFFailedToLoad();
    }
    if (mFFmpegFailedToLoad) {
      aDiagnostics->SetFFmpegFailedToLoad();
    }
    if (mGMPPDMFailedToStartup) {
      aDiagnostics->SetGMPPDMFailedToStartup();
    }
  }

  RefPtr<PlatformDecoderModule> pdm;
  for (auto& current : mCurrentPDMs) {
    if (current->SupportsMimeType(aMimeType, aDiagnostics)) {
      pdm = current;
      break;
    }
  }
  return pdm.forget();
}

#ifdef MOZ_EME
void
PDMFactory::SetCDMProxy(CDMProxy* aProxy)
{
  RefPtr<PDMFactory> m = new PDMFactory();
  mEMEPDM = new EMEDecoderModule(aProxy, m);
}
#endif

}  // namespace mozilla
