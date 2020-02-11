/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegRuntimeLinker.h"
#include "mozilla/ArrayUtils.h"
#include "FFmpegLog.h"
#include "prlink.h"

namespace mozilla
{

FFmpegRuntimeLinker::LinkStatus FFmpegRuntimeLinker::sLinkStatus =
  LinkStatus_INIT;

template <int V> class FFmpegDecoderModule
{
public:
  static already_AddRefed<PlatformDecoderModule> Create();
};

#if defined(XP_WIN)
HMODULE FFmpegRuntimeLinker::avcd=NULL;
HMODULE FFmpegRuntimeLinker::avutl=NULL;
void* FFmpegRuntimeLinker::avc_ptr[avc_symbs_count];
#else
static const char* sLibs[] = {
#if defined(XP_DARWIN)
  "libavcodec.58.dylib",
  "libavcodec.57.dylib",
  "libavcodec.56.dylib",
  "libavcodec.55.dylib",
  "libavcodec.54.dylib",
  "libavcodec.53.dylib",
#else
  "libavcodec.so.58",
  "libavcodec-ffmpeg.so.58",
  "libavcodec-ffmpeg.so.57",
  "libavcodec-ffmpeg.so.56",
  "libavcodec.so.57",
  "libavcodec.so.56",
  "libavcodec.so.55",
  "libavcodec.so.54",
  "libavcodec.so.53",
#endif
};

PRLibrary* FFmpegRuntimeLinker::sLinkedLib = nullptr;
const char* FFmpegRuntimeLinker::sLib = nullptr;
#define AV_FUNC(func, ver) void (*func)();
#define LIBAVCODEC_ALLVERSION
#include "FFmpegFunctionList.h"
#undef LIBAVCODEC_ALLVERSION
#undef AV_FUNC

#endif

static unsigned (*avcodec_version)() = nullptr;

/* static */ bool
FFmpegRuntimeLinker::Link()
{
  if (sLinkStatus) {
    return sLinkStatus == LinkStatus_SUCCEEDED;
  }

  MOZ_ASSERT(NS_IsMainThread());

#if defined(XP_WIN)
  HKEY aKey;
  DWORD d;
  HMODULE hModule = nullptr;
  char* dllpath = new char[1000];

  if(::RegOpenKeyExA(HKEY_CLASSES_ROOT,
    "CLSID\\{171252A0-8820-4AFE-9DF8-5C92B2D66B04}\\InprocServer32",
     0,KEY_READ, &aKey)!=0) { /*return false;*/ }

  if(::RegQueryValueExA(aKey, NULL, NULL, NULL, (LPBYTE)dllpath, &d)!=0) {
    /*return false;*/
    /*::lstrcpyA(&dllpath[0], ".\"); /* try to load bundled ffmpeg libraries */
trybundled:
    hModule = ::GetModuleHandle(NULL);
    if(hModule == NULL) return false; /* can't get self handle, bail out */
    ::GetModuleFileNameA(hModule, dllpath, 1000);
    d = ::lstrlenA(dllpath);
  }

  ::RegCloseKey(aKey);

  int i = (int)d;
	do{
		i--;
	} while (dllpath[i]!='\\'&&i>0);
  i++;
  ::lstrcpyA(&dllpath[i], "avcodec-lav-57.dll");
  avcd=::LoadLibraryExA(dllpath,0,LOAD_WITH_ALTERED_SEARCH_PATH);
  ::lstrcpyA(&dllpath[i], "avutil-lav-55.dll");
  avutl=::LoadLibraryExA(dllpath,0,LOAD_WITH_ALTERED_SEARCH_PATH);

  if(avcd==NULL||avutl==NULL){
     if(!hModule) goto trybundled;
     delete [] dllpath;
     return false;
     } 

  HMODULE hmod=avcd;

  const char* avc_symbs[]={
  "avcodec_version",
  "avcodec_alloc_context3",
  "avcodec_close",
  "avcodec_decode_audio4",
  "avcodec_decode_video2",
  "avcodec_find_decoder",
  "avcodec_flush_buffers",
  "avcodec_open2",
  "avcodec_register_all",
  "av_init_packet",
  "av_parser_init",
  "av_parser_close",
  "av_parser_parse2",
  /* libavutil */
  "av_log_set_level",
  "av_freep",
  "av_frame_alloc",
  "av_frame_free",
  "av_frame_unref"
  };

  i=0;

  do{
  if (i==13) hmod=avutl;
    avc_ptr[i] = GetProcAddress(hmod,avc_symbs[i]);
    if (avc_ptr[i]==NULL){
      ::GetModuleFileNameA(hmod,dllpath,1000);
      d=::lstrlenA(dllpath);
  	do{
		d--;
	} while (dllpath[i]!='\\'&&i>0);
      FFMPEG_LOG("Couldn't load function ",avc_symbs[i]," from %s.", &dllpath[d]);
      delete [] dllpath;
      return false;
     }
    i++;
  }while(i<avc_symbs_count);
  delete [] dllpath;
  avcodec_version = (decltype(avcodec_version))avc_ptr[0];
  sLinkStatus = LinkStatus_SUCCEEDED;
  return true;

#else

  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
    const char* lib = sLibs[i];
    PRLibSpec lspec;
    lspec.type = PR_LibSpec_Pathname;
    lspec.value.pathname = lib;
    sLinkedLib = PR_LoadLibraryWithFlags(lspec, PR_LD_NOW | PR_LD_LOCAL);
    if (sLinkedLib) {
      if (Bind(lib)) {
        sLib = lib;
        sLinkStatus = LinkStatus_SUCCEEDED;
        return true;
      }
      // Shouldn't happen but if it does then we try the next lib..
      Unlink();
    }
  }

  FFMPEG_LOG("H264/AAC codecs unsupported without [");
  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
    FFMPEG_LOG("%s %s", i ? "," : " ", sLibs[i]);
  }
  FFMPEG_LOG(" ]\n");

  Unlink();

  sLinkStatus = LinkStatus_FAILED;
  return false;
#endif
}

#if !defined(XP_WIN)
/* static */ bool
FFmpegRuntimeLinker::Bind(const char* aLibName)
{
  avcodec_version = (typeof(avcodec_version))PR_FindSymbol(sLinkedLib,
                                                           "avcodec_version");
  uint32_t fullVersion, major, minor, micro;
  fullVersion = GetVersion(major, minor, micro);
  if (!fullVersion) {
    return false;
  }

  if (micro < 100 &&
      fullVersion < (54u << 16 | 35u << 8 | 1u) &&
      !Preferences::GetBool("media.libavcodec.allow-obsolete", false)) {
    // Refuse any libavcodec version prior to 54.35.1.
    // (Unless media.libavcodec.allow-obsolete==true)
    Unlink();
    return false;
  }

  int version;
  switch (major) {
    case 53:
      version = AV_FUNC_53;
      break;
    case 54:
      version = AV_FUNC_54;
      break;
    case 56:
      // We use libavcodec 55 code instead. Fallback.
    case 55:
      version = AV_FUNC_55;
      break;
    case 57:
      if (micro < 100) {
        // A micro version >= 100 indicates that it's FFmpeg (as opposed to LibAV).
        // Due to current AVCodecContext binary incompatibility we can only
        // support FFmpeg at this point.
        return false;
      }
      version = AV_FUNC_57;
      break;
    case 58:
      if (micro < 100) {
        // A micro version >= 100 indicates that it's FFmpeg (as opposed to LibAV).
        // Due to current AVCodecContext binary incompatibility we can only
        // support FFmpeg at this point.
        return false;
      }
      version = AV_FUNC_58;
      break;
    default:
      // Not supported at this stage.
      return false;
  }

#define LIBAVCODEC_ALLVERSION
#define AV_FUNC(func, ver)                                                     \
  if ((ver) & version) {                                                       \
    if (!(func = (typeof(func))PR_FindSymbol(sLinkedLib, #func))) {            \
      FFMPEG_LOG("Couldn't load function " #func " from %s.", aLibName);       \
      return false;                                                            \
    }                                                                          \
  } else {                                                                     \
    func = (typeof(func))nullptr;                                              \
  }
#include "FFmpegFunctionList.h"
#undef AV_FUNC
#undef LIBAVCODEC_ALLVERSION
  return true;
}
#endif

/* static */ already_AddRefed<PlatformDecoderModule>
FFmpegRuntimeLinker::CreateDecoderModule()
{
  if (!Link()) {
    return nullptr;
  }
  uint32_t major, minor, micro;
  if (!GetVersion(major, minor, micro)) {
    return  nullptr;
  }
  nsRefPtr<PlatformDecoderModule> module;
  switch (major) {
#if !defined(XP_WIN)
    case 53: module = FFmpegDecoderModule<53>::Create(); break;
    case 54: module = FFmpegDecoderModule<54>::Create(); break;
    case 55:
    case 56: module = FFmpegDecoderModule<55>::Create(); break;
#endif
    case 57: module = FFmpegDecoderModule<57>::Create(); break;
    case 58: module = FFmpegDecoderModule<58>::Create(); break;
    default: module = nullptr;
  }
  return module.forget();
}

/* static */ void
FFmpegRuntimeLinker::Unlink()
{
#if defined(XP_WIN)
    ::FreeLibrary(avcd);
    ::FreeLibrary(avutl);
    sLinkStatus = LinkStatus_INIT;
    avcodec_version = nullptr;
#else
  if (sLinkedLib) {
    PR_UnloadLibrary(sLinkedLib);
    sLinkedLib = nullptr;
    sLib = nullptr;
    sLinkStatus = LinkStatus_INIT;
    avcodec_version = nullptr;
  }
#endif
}

/* static */ uint32_t
FFmpegRuntimeLinker::GetVersion(uint32_t& aMajor, uint32_t& aMinor, uint32_t& aMicro)
{
  if (!avcodec_version) {
    return 0u;
  }
  uint32_t version = avcodec_version();
  aMajor = (version >> 16) & 0xff;
  aMinor = (version >> 8) & 0xff;
  aMicro = version & 0xff;
  return version;
}

} // namespace mozilla
