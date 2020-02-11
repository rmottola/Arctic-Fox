/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __FFmpegRuntimeLinker_h__
#define __FFmpegRuntimeLinker_h__

#include "PlatformDecoderModule.h"
#include <stdint.h>
#if defined(XP_WIN)
#include "windows.h"
#else
struct PRLibrary;
#endif

namespace mozilla
{
#if defined(XP_WIN)

#define avc_symbs_count 18
#define _version	0
#define _alloc_context3 1
#define _close          2
#define _decode_audio4  3
#define _decode_video2  4
#define _find_decoder   5
#define _flush_buffers  6 
#define _open2          7
#define _register_all   8
#define _init_packet    9
#define _parser_init    10
#define _parser_close   11
#define _parser_parse2  12
/* libavutil */
#define _log_set_level  13
#define _freep          14
#define _frame_alloc    15
#define _frame_free     16
#define _frame_unref    17

#else

enum {
  AV_FUNC_AVUTIL_MASK = 1 << 8,
  AV_FUNC_53 = 1 << 0,
  AV_FUNC_54 = 1 << 1,
  AV_FUNC_55 = 1 << 2,
  AV_FUNC_56 = 1 << 3,
  AV_FUNC_57 = 1 << 4,
  AV_FUNC_58 = 1 << 5,
  AV_FUNC_AVUTIL_53 = AV_FUNC_53 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVUTIL_54 = AV_FUNC_54 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVUTIL_55 = AV_FUNC_55 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVUTIL_56 = AV_FUNC_56 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVUTIL_57 = AV_FUNC_57 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVUTIL_58 = AV_FUNC_58 | AV_FUNC_AVUTIL_MASK,
  AV_FUNC_AVCODEC_ALL = AV_FUNC_53 | AV_FUNC_54 | AV_FUNC_55 | AV_FUNC_56 | AV_FUNC_57 | AV_FUNC_58,
  AV_FUNC_AVUTIL_ALL = AV_FUNC_AVCODEC_ALL | AV_FUNC_AVUTIL_MASK
};

#endif

class FFmpegRuntimeLinker
{
public:
  static bool Link();
  static void Unlink();
  static already_AddRefed<PlatformDecoderModule> CreateDecoderModule();
  static uint32_t GetVersion(uint32_t& aMajor, uint32_t& aMinor, uint32_t& aMicro);
#if defined(XP_WIN)
  static void* avc_ptr[avc_symbs_count];
#endif
private:
#if defined(XP_WIN)
  static HMODULE avcd;
  static HMODULE avutl;
#else
  static PRLibrary* sLinkedLib;
  static const char* sLib;
  static bool Bind(const char* aLibName);
#endif
  static enum LinkStatus {
    LinkStatus_INIT = 0,
    LinkStatus_FAILED,
    LinkStatus_SUCCEEDED
  } sLinkStatus;
};

}

#endif // __FFmpegRuntimeLinker_h__
