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

ERR WriteContainerPre(
    PKImageEncode* pIE);

ERR WriteContainerPost(
    PKImageEncode* pIE);

ERR PKImageEncode_Transcode_WMP(
    PKImageEncode* pIE,
    PKImageDecode* pID,
    CWMTranscodingParam* pParam)
{
    ERR err = WMP_errSuccess;
    Float fResX = 0, fResY = 0;
    PKPixelFormatGUID pixGUID = {0};
    CWMTranscodingParam tcParamAlpha;
    size_t offPos = 0;
    Bool fPlanarAlpha;
    PKPixelInfo PI;

    struct WMPStream* pWSDec = NULL;
    struct WMPStream* pWSEnc= pIE->pStream;

    // pass through metadata
    Call(pID->GetPixelFormat(pID, &pixGUID));
    Call(pIE->SetPixelFormat(pIE, pixGUID));

    Call(pIE->SetSize(pIE, (I32)pParam->cWidth, (I32)pParam->cHeight));

    Call(pID->GetResolution(pID, &fResX, &fResY));
    Call(pIE->SetResolution(pIE, fResX, fResY));

    PI.pGUIDPixFmt = &pIE->guidPixFormat;
    PixelFormatLookup(&PI, LOOKUP_FORWARD);
    pIE->WMP.bHasAlpha = !!(PI.grBit & PK_pixfmtHasAlpha) && (2 == pParam->uAlphaMode);
    assert(0 == pIE->WMP.bHasAlpha || (pParam->uAlphaMode == 2)); // Decode alpha mode does not match encode alpha mode!

    // Check for any situations where transcoder is being asked to convert alpha - we can't do this
    // NOTE: Decoder's bHasAlpha parameter really means, "has PLANAR alpha"
    PI.pGUIDPixFmt = &pixGUID;
    PixelFormatLookup(&PI, LOOKUP_FORWARD);
    FailIf(0 == (PI.grBit & PK_pixfmtHasAlpha) && pParam->uAlphaMode != 0,
        WMP_errAlphaModeCannotBeTranscoded); // Destination is planar/interleaved, src has no alpha
    FailIf(!!(PI.grBit & PK_pixfmtHasAlpha) && 2 == pParam->uAlphaMode &&
        FALSE == pID->WMP.bHasAlpha, WMP_errAlphaModeCannotBeTranscoded);  // Destination is planar, src is interleaved
    FailIf(!!(PI.grBit & PK_pixfmtHasAlpha) && 3 == pParam->uAlphaMode &&
        pID->WMP.bHasAlpha, WMP_errAlphaModeCannotBeTranscoded);  // Destination is interleaved, src is planar
    assert(//pParam->uAlphaMode >= 0 &&
        pParam->uAlphaMode <= 3); // All the above statements make this assumption

    fPlanarAlpha = pIE->WMP.bHasAlpha && (2 == pParam->uAlphaMode);

    // write matadata
    Call(WriteContainerPre(pIE));

    // Copy transcoding params for alpha (codec changes the struct)
    if (fPlanarAlpha)
        tcParamAlpha = *pParam;

    // write compressed bitstream
    Call(pID->GetRawStream(pID, &pWSDec));

    /////////////////////
    pParam->uImageOffset = pID->WMP.wmiDEMisc.uImageOffset;
    pParam->uImageByteCount = pID->WMP.wmiDEMisc.uImageByteCount;
    ////////////

    FailIf(ICERR_OK != WMPhotoTranscode(pWSDec, pWSEnc, pParam), WMP_errFail);
    Call(pIE->pStream->GetPos(pIE->pStream, &offPos));
    pIE->WMP.nCbImage = (Long)offPos - pIE->WMP.nOffImage;

    if (fPlanarAlpha)
    {
        /////////////////////
        pParam->uImageOffset = pID->WMP.wmiDEMisc.uAlphaOffset;
        pParam->uImageByteCount = pID->WMP.wmiDEMisc.uAlphaByteCount;
        ////////////

        pIE->WMP.nOffAlpha = (Long)offPos;

        // Cue the stream to alpha block
        assert(pID->WMP.wmiDEMisc.uAlphaOffset > 0);
        Call(pWSDec->SetPos(pWSDec, pID->WMP.wmiDEMisc.uAlphaOffset));

        FailIf(ICERR_OK != WMPhotoTranscode(pWSDec, pWSEnc, &tcParamAlpha), WMP_errFail);
        Call(pIE->pStream->GetPos(pIE->pStream, &offPos));
        pIE->WMP.nCbAlpha = (Long)offPos - pIE->WMP.nOffAlpha;
    }

    // fixup matadata
    Call(WriteContainerPost(pIE));

Cleanup:
    return err;
}

