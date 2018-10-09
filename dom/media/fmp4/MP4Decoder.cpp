/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MP4Decoder.h"
#include "MP4Reader.h"
#include "MediaDecoderStateMachine.h"
#include "MediaFormatReader.h"
#include "MP4Demuxer.h"
#include "mozilla/Preferences.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentTypeParser.h"
#include "VideoUtils.h"
#include "prlog.h"

#ifdef XP_WIN
#include "mozilla/WindowsVersion.h"
#include "WMFDecoderModule.h"
#endif
#ifdef MOZ_WIDGET_ANDROID
#include "nsIGfxInfo.h"
#include "AndroidBridge.h"
#endif
#ifdef MOZ_APPLEMEDIA
#include "AppleDecoderModule.h"
#endif
#ifdef MOZ_FFMPEG
#include "FFmpegRuntimeLinker.h"
#endif

namespace mozilla {

MediaDecoderStateMachine* MP4Decoder::CreateStateMachine()
{
  bool useFormatDecoder =
    Preferences::GetBool("media.format-reader.mp4", true);
  nsRefPtr<MediaDecoderReader> reader = useFormatDecoder ?
    static_cast<MediaDecoderReader*>(new MediaFormatReader(this, new MP4Demuxer(GetResource()))) :
    static_cast<MediaDecoderReader*>(new MP4Reader(this));

  return new MediaDecoderStateMachine(this, reader);
}

static bool
IsWhitelistedH264Codec(const nsAString& aCodec)
{
  int16_t profile = 0, level = 0;

  if (!ExtractH264CodecDetails(aCodec, profile, level)) {
    return false;
  }

#ifdef XP_WIN
  if (!Preferences::GetBool("media.use-blank-decoder") &&
      !WMFDecoderModule::HasH264()) {
    return false;
  }

  // Disable 4k video on windows vista since it performs poorly.
  if (!IsWin7OrLater() &&
      level >= H264_LEVEL_5) {
    return false;
  }
#endif

  // Just assume what we can play on all platforms the codecs/formats that
  // WMF can play, since we don't have documentation about what other
  // platforms can play... According to the WMF documentation:
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd797815%28v=vs.85%29.aspx
  // "The Media Foundation H.264 video decoder is a Media Foundation Transform
  // that supports decoding of Baseline, Main, and High profiles, up to level
  // 5.1.". We also report that we can play Extended profile, as there are
  // bitstreams that are Extended compliant that are also Baseline compliant.
  return level >= H264_LEVEL_1 &&
         level <= H264_LEVEL_5_1 &&
         (profile == H264_PROFILE_BASE ||
          profile == H264_PROFILE_MAIN ||
          profile == H264_PROFILE_EXTENDED ||
          profile == H264_PROFILE_HIGH);
}

/* static */
bool
MP4Decoder::CanHandleMediaType(const nsACString& aMIMETypeExcludingCodecs,
                               const nsAString& aCodecs)
{
  if (!IsEnabled()) {
    return false;
  }

  // Whitelist MP4 types, so they explicitly match what we encounter on
  // the web, as opposed to what we use internally (i.e. what our demuxers
  // etc output).
  const bool isMP4Audio = aMIMETypeExcludingCodecs.EqualsASCII("audio/mp4") ||
                          aMIMETypeExcludingCodecs.EqualsASCII("audio/x-m4a");
  const bool isMP4Video = aMIMETypeExcludingCodecs.EqualsASCII("video/mp4") ||
#ifdef XP_LINUX
                          aMIMETypeExcludingCodecs.EqualsASCII("video/quicktime") ||
#endif
                          aMIMETypeExcludingCodecs.EqualsASCII("video/x-m4v");
  if (!isMP4Audio && !isMP4Video) {
    return false;
  }

  #ifdef MOZ_GONK_MEDIACODEC
  if (aMIMETypeExcludingCodecs.EqualsASCII(VIDEO_3GPP)) {
    return Preferences::GetBool("media.gonk.enabled", false);
  }
#endif

  nsTArray<nsCString> codecMimes;
  if (aCodecs.IsEmpty()) {
    // No codecs specified. Assume AAC/H.264
    if (isMP4Audio) {
      codecMimes.AppendElement(NS_LITERAL_CSTRING("audio/mp4a-latm"));
    } else {
      MOZ_ASSERT(isMP4Video);
      codecMimes.AppendElement(NS_LITERAL_CSTRING("video/avc"));
    }
  } else {
    // Verify that all the codecs specified are ones that we expect that
    // we can play.
    nsTArray<nsString> codecs;
    if (!ParseCodecsString(aCodecs, codecs)) {
      return false;
    }
    for (const nsString& codec : codecs) {
      if (IsAACCodecString(codec)) {
        codecMimes.AppendElement(NS_LITERAL_CSTRING("audio/mp4a-latm"));
        continue;
      }
      if (codec.EqualsLiteral("mp3")) {
        codecMimes.AppendElement(NS_LITERAL_CSTRING("audio/mpeg"));
        continue;
      }
      // Note: Only accept H.264 in a video content type, not in an audio
      // content type.
      if (IsWhitelistedH264Codec(codec) && isMP4Video) {
        codecMimes.AppendElement(NS_LITERAL_CSTRING("video/avc"));
        continue;
      }
      // Some unsupported codec.
      return false;
    }
  }

  // Verify that we have a PDM that supports the whitelisted types.
  PlatformDecoderModule::Init();
  nsRefPtr<PlatformDecoderModule> platform = PlatformDecoderModule::Create();
  if (!platform) {
    return false;
  }
  for (const nsCString& codecMime : codecMimes) {
    if (!platform->SupportsMimeType(codecMime)) {
      return false;
    }
  }

  return true;
}

/* static */ bool
MP4Decoder::CanHandleMediaType(const nsAString& aContentType)
{
  nsContentTypeParser parser(aContentType);
  nsAutoString mimeType;
  nsresult rv = parser.GetType(mimeType);
  if (NS_FAILED(rv)) {
    return false;
  }
  nsString codecs;
  parser.GetParameter("codecs", codecs);

  return CanHandleMediaType(NS_ConvertUTF16toUTF8(mimeType), codecs);
}

static bool
IsFFmpegAvailable()
{
#ifndef MOZ_FFMPEG
  return false;
#else
  if (!Preferences::GetBool("media.ffmpeg.enabled", false)) {
    return false;
  }
  PlatformDecoderModule::Init();
  nsRefPtr<PlatformDecoderModule> m = FFmpegRuntimeLinker::CreateDecoderModule();
  return !!m;
#endif
}

static bool
IsAppleAvailable()
{
#ifndef MOZ_APPLEMEDIA
  // Not the right platform.
  return false;
#else
  return Preferences::GetBool("media.apple.mp4.enabled", false);
#endif
}

static bool
IsAndroidAvailable()
{
#ifndef MOZ_WIDGET_ANDROID
  return false;
#else
  // We need android.media.MediaCodec which exists in API level 16 and higher.
  return AndroidBridge::Bridge()->GetAPIVersion() >= 16;
#endif
}

static bool
IsGonkMP4DecoderAvailable()
{
  return Preferences::GetBool("media.gonk.enabled", false);
}

static bool
IsGMPDecoderAvailable()
{
  return Preferences::GetBool("media.gmp.decoder.enabled", false);
}

static bool
HavePlatformMPEGDecoders()
{
#ifdef XP_WIN
  // We always have H.264/AAC platform decoders on Windows.
  return true;
#else
  return Preferences::GetBool("media.use-blank-decoder") ||
         IsAndroidAvailable() ||
         IsFFmpegAvailable() ||
         IsAppleAvailable() ||
         IsGonkMP4DecoderAvailable() ||
         IsGMPDecoderAvailable() ||
         // TODO: Other platforms...
         false;
#endif
}

/* static */
bool
MP4Decoder::IsEnabled()
{
  return Preferences::GetBool("media.fragmented-mp4.enabled") &&
         HavePlatformMPEGDecoders();
}

} // namespace mozilla

