// Copyright © Microsoft Corp.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// • Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// • Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#include "JXRGlue.h"

ERR PKCodecFactory_CreateDecoderFromFile(const char* szFilename, PKImageDecode** ppDecoder)
{
    ERR err = WMP_errSuccess;

    char *pExt = NULL;
    const PKIID* pIID = NULL;

    struct WMPStream* pStream = NULL;
    PKImageDecode* pDecoder = NULL;

    // get file extension
    pExt = strrchr(szFilename, '.');
    FailIf(NULL == pExt, WMP_errUnsupportedFormat);

    // get decode PKIID
    Call(GetImageDecodeIID(pExt, &pIID));

    // create stream
    Call(CreateWS_File(&pStream, szFilename, "rb"));

    // Create decoder
#if 0
    Call(PKCodecFactory_CreateCodec(pIID, (void **) ppDecoder));
#else
    // We know we are creating a decoder here
    Call(PKImageDecode_Create_WMP(ppDecoder));
#endif

    pDecoder = *ppDecoder;

    // attach stream to decoder
    Call(pDecoder->Initialize(pDecoder, pStream));
    pDecoder->fStreamOwner = !0;

Cleanup:
    return err;
}