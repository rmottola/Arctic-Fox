
//*@@@+++@@@@******************************************************************
//
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
//
//*@@@---@@@@******************************************************************
//@#include <limits.h>
#include "JXRGlue.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef USHRT_MAX
#define USHRT_MAX 0xFFFF
#endif

//================================================================
// PKImageDecode_WMP
//================================================================
static ERR ParsePFDEntry(
    PKImageDecode* pID,
    U16 uTag,
    U16 uType,
    U32 uCount,
    U32 uValue)
{
    ERR err = WMP_errSuccess;
    ERR errTmp = WMP_errSuccess;
    PKPixelInfo PI;
    struct WMPStream* pWS = pID->pStream;
    // size_t offPos = 0;

    union uf{
        U32 uVal;
        Float fVal;
    }ufValue = {0};

    //================================
    switch (uTag)
    {
        case WMP_tagPixelFormat:
        {
            unsigned char *pGuid = (unsigned char *) &pID->guidPixFormat;
            /** following code is endian-agnostic **/
            Call(GetULong(pWS, uValue, (U32 *)pGuid));
            Call(GetUShort(pWS, uValue + 4, (unsigned short *)(pGuid + 4)));
            Call(GetUShort(pWS, uValue + 6, (unsigned short *)(pGuid + 6)));
            Call(pWS->Read(pWS, pGuid + 8, 8));
                
            PI.pGUIDPixFmt = &pID->guidPixFormat;
            PixelFormatLookup(&PI, LOOKUP_FORWARD);

            pID->WMP.bHasAlpha = !!(PI.grBit & PK_pixfmtHasAlpha);
            pID->WMP.wmiI.cBitsPerUnit = PI.cbitUnit;
            pID->WMP.wmiI.bRGB = !(PI.grBit & PK_pixfmtBGR);

            break;
        }

        case WMP_tagTransformation:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            assert(uValue < O_MAX);
            pID->WMP.fOrientationFromContainer = TRUE;
            pID->WMP.oOrientationFromContainer = uValue;
            break;

        case WMP_tagImageWidth:
            FailIf(0 == uValue, WMP_errUnsupportedFormat);
            pID->uWidth = uValue;
            break;

        case WMP_tagImageHeight:
            FailIf(0 == uValue, WMP_errUnsupportedFormat);
            pID->uHeight = uValue;
            break;

        case WMP_tagImageOffset:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            pID->WMP.wmiDEMisc.uImageOffset = uValue;
            break;

        case WMP_tagImageByteCount:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            pID->WMP.wmiDEMisc.uImageByteCount = uValue;
            pID->WMP.wmiI.uImageByteCount = uValue;
            break;

        case WMP_tagAlphaOffset:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            pID->WMP.wmiDEMisc.uAlphaOffset = uValue;
            break;

        case WMP_tagAlphaByteCount:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            pID->WMP.wmiDEMisc.uAlphaByteCount = uValue;
            pID->WMP.wmiI_Alpha.uImageByteCount = uValue;
            break;

        case WMP_tagWidthResolution:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            ufValue.uVal = uValue; 
            pID->fResX = ufValue.fVal;
            break;

        case WMP_tagHeightResolution:
            FailIf(1 != uCount, WMP_errUnsupportedFormat);
            ufValue.uVal = uValue; 
            pID->fResY = ufValue.fVal;
            break;

        case WMP_tagIccProfile:
            pID->WMP.wmiDEMisc.uColorProfileByteCount = uCount;
            pID->WMP.wmiDEMisc.uColorProfileOffset = uValue;
            break;

        case WMP_tagXMPMetadata:
            pID->WMP.wmiDEMisc.uXMPMetadataByteCount = uCount;
            pID->WMP.wmiDEMisc.uXMPMetadataOffset = uValue;
            break;

        case WMP_tagEXIFMetadata:
            pID->WMP.wmiDEMisc.uEXIFMetadataOffset = uValue;
            CallIgnoreError(errTmp, StreamCalcIFDSize(pWS, uValue, &pID->WMP.wmiDEMisc.uEXIFMetadataByteCount));
            break;

        case WMP_tagGPSInfoMetadata:
            pID->WMP.wmiDEMisc.uGPSInfoMetadataOffset = uValue;
            CallIgnoreError(errTmp, StreamCalcIFDSize(pWS, uValue, &pID->WMP.wmiDEMisc.uGPSInfoMetadataByteCount));
            break;

        case WMP_tagIPTCNAAMetadata:
            pID->WMP.wmiDEMisc.uIPTCNAAMetadataByteCount = uCount;
            pID->WMP.wmiDEMisc.uIPTCNAAMetadataOffset = uValue;
            break;

        case WMP_tagPhotoshopMetadata:
            pID->WMP.wmiDEMisc.uPhotoshopMetadataByteCount = uCount;
            pID->WMP.wmiDEMisc.uPhotoshopMetadataOffset = uValue;
            break;

        case WMP_tagCompression:
        case WMP_tagImageType:
        case WMP_tagImageDataDiscard:
        case WMP_tagAlphaDataDiscard:
            break;

        // Descriptive Metadata
        case WMP_tagImageDescription:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarImageDescription));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarImageDescription.vt);
            break;

        case WMP_tagCameraMake:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarCameraMake));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarCameraMake.vt);
            break;

        case WMP_tagCameraModel:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarCameraModel));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarCameraModel.vt);
            break;

        case WMP_tagSoftware:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarSoftware));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarSoftware.vt);
            break;

        case WMP_tagDateTime:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarDateTime));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarDateTime.vt);
            break;

        case WMP_tagArtist:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarArtist));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarArtist.vt);
            break;

        case WMP_tagCopyright:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarCopyright));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarCopyright.vt);
            break;

        case WMP_tagRatingStars:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarRatingStars));
            assert(DPKVT_UI2 == pID->WMP.sDescMetadata.pvarRatingStars.vt);
            break;

        case WMP_tagRatingValue:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarRatingValue));
            assert(DPKVT_UI2 == pID->WMP.sDescMetadata.pvarRatingValue.vt);
            break;

        case WMP_tagCaption:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarCaption));
            assert((DPKVT_BYREF | DPKVT_UI1) == pID->WMP.sDescMetadata.pvarCaption.vt);

            // Change type from C-style byte array to LPWSTR
            assert((U8*)pID->WMP.sDescMetadata.pvarCaption.VT.pwszVal ==
                pID->WMP.sDescMetadata.pvarCaption.VT.pbVal);
            assert(0 == pID->WMP.sDescMetadata.pvarCaption.VT.pwszVal[uCount/sizeof(U16) - 1]); // Confirm null-term
            //  make sure null term (ReadPropvar allocated enough space for this)
            pID->WMP.sDescMetadata.pvarCaption.VT.pwszVal[uCount/sizeof(U16)] = 0;
            pID->WMP.sDescMetadata.pvarCaption.vt = DPKVT_LPWSTR;
            break;

        case WMP_tagDocumentName:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarDocumentName));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarDocumentName.vt);
            break;

        case WMP_tagPageName:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarPageName));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarPageName.vt);
            break;

        case WMP_tagPageNumber:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarPageNumber));
            assert(DPKVT_UI4 == pID->WMP.sDescMetadata.pvarPageNumber.vt);
            break;

        case WMP_tagHostComputer:
            CallIgnoreError(errTmp, ReadPropvar(pWS, uType, uCount, uValue,
                &pID->WMP.sDescMetadata.pvarHostComputer));
            assert(DPKVT_LPSTR == pID->WMP.sDescMetadata.pvarHostComputer.vt);
            break;

        default:
            fprintf(stderr, "Unrecognized WMPTag: %d(%#x), %d, %d, %#x" CRLF,
                (int)uTag, (int)uTag, (int)uType, (int)uCount, (int)uValue);
            break;
    }

Cleanup:
    return err;
}

static ERR ParsePFD(
    PKImageDecode* pID,
    size_t offPos,
    U16 cEntry)
{
    ERR err = WMP_errSuccess;
    struct WMPStream* pWS = pID->pStream;
    U16 i = 0;

    for (i = 0; i < cEntry; ++i)
    {
        U16 uTag = 0;
        U16 uType = 0;
        U32 uCount = 0;
        U32 uValue = 0;

        Call(GetUShort(pWS, offPos, &uTag)); offPos += 2;
        Call(GetUShort(pWS, offPos, &uType)); offPos += 2;
        Call(GetULong(pWS, offPos, &uCount)); offPos += 4;
        Call(GetULong(pWS, offPos, &uValue)); offPos += 4;

        Call(ParsePFDEntry(pID, uTag, uType, uCount, uValue)); 
    }

    pID->WMP.bHasAlpha = ((pID->WMP.bHasAlpha) && (pID->WMP.wmiDEMisc.uAlphaOffset != 0) && (pID->WMP.wmiDEMisc.uAlphaByteCount != 0));//has planar alpha

Cleanup:
    return err;
}

ERR ReadContainer(
    PKImageDecode* pID)
{
    ERR err = WMP_errSuccess;

    struct WMPStream* pWS = pID->pStream;
    size_t offPos = 0;

    char szSig[2] = {0};
    U16 uWmpID = 0;
    U32 offPFD = 0;
    U16 cPFDEntry = 0;
    U8 bVersion;
    
    //================================
    Call(pWS->GetPos(pWS, &offPos));
    FailIf(0 != offPos, WMP_errUnsupportedFormat);

    //================================
    // Header
    Call(pWS->Read(pWS, szSig, sizeof(szSig))); offPos += 2;
    FailIf(szSig != strstr(szSig, "II"), WMP_errUnsupportedFormat);

    Call(GetUShort(pWS, offPos, &uWmpID)); offPos += 2;
    FailIf(WMP_valWMPhotoID != (0x00FF & uWmpID), WMP_errUnsupportedFormat);

    // We accept version 00 and version 01 bitstreams - all others rejected
    bVersion = (0xFF00 & uWmpID) >> 8;
    FailIf(bVersion != 0 && bVersion != 1, WMP_errUnsupportedFormat);

    Call(GetULong(pWS, offPos, &offPFD)); offPos += 4;

    //================================
    // PFD
    offPos = (size_t)offPFD;
    Call(GetUShort(pWS, offPos, &cPFDEntry)); offPos += 2;
    FailIf(0 == cPFDEntry || USHRT_MAX == cPFDEntry, WMP_errUnsupportedFormat);
    Call(ParsePFD(pID, offPos, cPFDEntry));

Cleanup:
    return err;
}


//================================================
ERR PKImageDecode_Initialize_WMP(
    PKImageDecode* pID,
    struct WMPStream* pWS)
{
    ERR err = WMP_errSuccess;

    CWMImageInfo* pII = NULL;

    //================================
    Call(PKImageDecode_Initialize(pID, pWS));

    //================================
    Call(ReadContainer(pID));

    Call(pWS->SetPos(pWS, pID->WMP.wmiDEMisc.uImageOffset)); // Moved here from ReadContainer()

    //================================
    pID->WMP.wmiSCP.pWStream = pWS;
    pID->WMP.wmiSCP.uStreamImageOffset = 0;
    pID->WMP.wmiSCP_Alpha.uStreamImageOffset = 0;
    pID->WMP.DecoderCurrMBRow = 0;
    pID->WMP.DecoderCurrMBRow_Alpha = 0;
    pID->WMP.cLinesDecoded = 0;
    pID->WMP.cLinesCropped = 0;
    pID->WMP.fFirstNonZeroDecode = FALSE;
    pID->WMP.cLinesCropped_Alpha = 0;
    pID->WMP.fFirstNonZeroDecode_Alpha = FALSE;

    FailIf(ICERR_OK != ImageStrDecGetInfo(&pID->WMP.wmiI, &pID->WMP.wmiSCP), WMP_errFail);

    pII = &pID->WMP.wmiI;

    assert(Y_ONLY <= pID->WMP.wmiSCP.cfColorFormat && pID->WMP.wmiSCP.cfColorFormat < CFT_MAX);
    assert(BD_SHORT == pID->WMP.wmiSCP.bdBitDepth || BD_LONG == pID->WMP.wmiSCP.bdBitDepth);

    // If HD Photo container provided an orientation, this should override bitstream orientation
    // If container did NOT provide an orientation, force O_NONE. This is to be consistent with
    // Vista behaviour, which is to ignore bitstream orientation (only looks at container).
    if (pID->WMP.fOrientationFromContainer)
    {
        pID->WMP.wmiI.oOrientation = pID->WMP.oOrientationFromContainer;
    }
    else
    {
        // Force to O_NONE to match Vista decode behaviour
        pID->WMP.wmiI.oOrientation = O_NONE;
    }

    pID->uWidth = (U32)pII->cWidth;
    pID->uHeight = (U32)pII->cHeight;

Cleanup:
    return err;
}


ERR PKImageDecode_GetSize_WMP(
    PKImageDecode* pID,
    I32* piWidth,
    I32* piHeight)
{
    if (pID->WMP.wmiI.oOrientation >= O_RCW)
    {
        *piWidth = (I32)pID->uHeight;
        *piHeight = (I32)pID->uWidth;
    }
    else
    {
        *piWidth = (I32)pID->uWidth;
        *piHeight = (I32)pID->uHeight;
    }
    return WMP_errSuccess;
}


ERR PKImageDecode_GetRawStream_WMP(
    PKImageDecode* pID,
    struct WMPStream** ppWS)
{
    ERR err = WMP_errSuccess;
    struct WMPStream* pWS = pID->pStream;

    *ppWS = NULL;
    Call(pWS->SetPos(pWS, pID->WMP.wmiDEMisc.uImageOffset));
    *ppWS = pWS;

Cleanup:
    return err;
}

// Translate ROI from user to image coordinates.
// imageWidth and imageHeight are original, not translated.
static void TranslateROI(const PKRect *pRectUser, PKRect *pRectImage, U32 imageWidth, U32 imageHeight, ORIENTATION or)
{
    Bool bReverse;
    *pRectImage = *pRectUser;

    if (or & O_RCW)
    {
        I32 tmp;
        tmp = pRectImage->Width;
        pRectImage->Width = pRectImage->Height;
        pRectImage->Height = tmp;

        tmp = pRectImage->X;
        pRectImage->X = pRectImage->Y;
        pRectImage->Y = tmp;
    }

    // The same logic as in initializeLookupTables()
    bReverse = (O_FLIPV == or || O_FLIPVH == or || O_RCW == or || O_RCW_FLIPV == or);

    if (bReverse)
        pRectImage->Y = imageHeight - pRectImage->Y - pRectImage->Height;

    // The same logic as in initializeLookupTables()
    bReverse = (O_FLIPH == or || O_FLIPVH == or || O_RCW_FLIPV == or || O_RCW_FLIPVH == or);

    if (bReverse)
        pRectImage->X = imageWidth - pRectImage->X - pRectImage->Width;
}

#ifdef REENTRANT_MODE

// Begin decoding main image plane.
// Output row stride will remain the same for all subsequent calls to JXR_DecodeNextMBRow().
// failSafe = TRUE causes the coding context to be saved before every attempt to decode a row of macroblocks
// when an image is not fully available (like in Web clients that try to decode as much of a partially downloaded image as possible).
// If an attempt was not successful (because there was not enough data), the coding context (which includes Huffmann tables
// and other data that gets modified during the decoding) is restored, and a new attempt to decode is made after more data is available.
ERR JXR_BeginDecodingMBRows(
    PKImageDecode *pID, 
    const PKRect *pRect, 
    U8 *pb, 
    size_t cbStride, 
    Bool fullROIBuffer
#ifdef WEB_CLIENT_SUPPORT
    , Bool failSafe
#endif
    )
{
    ERR err = WMP_errSuccess;
    CWMImageInfo *pWMII = &pID->WMP.wmiI;
    U8 tempAlphaMode = 0;
    size_t pos;
    PKRect roi;

    // In "Low Memory mode" (i.e. when the decode buffer can fit only one macroblock row), 
    // we don't have full frame buffer. We therefore cannot rotate the image.
    // We can flip H, V and HV, but no rotations.
    FailIf(!fullROIBuffer && pWMII->oOrientation >= O_RCW, WMP_errFail);

    // Set Region of Interest
    if (NULL == pRect) // the user did not specify ROI
        roi.X = roi.Y = roi.Width = roi.Height = 0; // a correct ROI will be set in WMPhotoValidate()
    else
    {
        CalcThumbnailSize(pWMII);
        TranslateROI(pRect, &roi, pWMII->cThumbnailWidth, pWMII->cThumbnailHeight, pWMII->oOrientation);
    }

    pWMII->cROILeftX = roi.X;
    pWMII->cROITopY = roi.Y;
    pWMII->cROIWidth = roi.Width;
    pWMII->cROIHeight = roi.Height;

    pID->WMP.DecoderCurrMBRow = 0;
    pID->WMP.cLinesCropped = 0;
    pID->WMP.fFirstNonZeroDecode = FALSE;

    // Set the fPaddedUserBuffer if the following conditions are met
    if (0 == ((size_t)pb % 128) &&    // Frame buffer is aligned to 128-byte boundary
        0 == (cbStride % 128))        // Stride is a multiple of 128 bytes
    {
        pWMII->fPaddedUserBuffer = TRUE;
        // Note that there are additional conditions in strdec_x86.c's strDecOpt
        // which could prevent optimization from being engaged
    }

    //if (pID->WMP.wmiSCP.uAlphaMode != 1)
    if ((!pID->WMP.bHasAlpha) || (pID->WMP.wmiSCP.uAlphaMode != 1))
    {
        if (pID->WMP.bHasAlpha) // planar alpha
        {
            tempAlphaMode = pID->WMP.wmiSCP.uAlphaMode;
            pID->WMP.wmiSCP.uAlphaMode = 0;
        }

        // The stream does not necessarily begin where the image file begins.
        // We should not expect that before calling this function the right stream position was set.
        // The caller should rather set pID->offStart. Calculate and set the stream position here.
        assert(pID->WMP.wmiDEMisc.uImageOffset >= pID->offStart);
        pos = pID->WMP.wmiDEMisc.uImageOffset - pID->offStart;
        pID->WMP.wmiSCP.uStreamImageOffset = pos;
        Call(pID->WMP.wmiSCP.pWStream->SetPos(pID->WMP.wmiSCP.pWStream, pos));

#ifdef WEB_CLIENT_SUPPORT
        FailIf(ICERR_OK != ImageStrDecInit(pWMII, &pID->WMP.wmiSCP, &pID->WMP.ctxSC, cbStride, failSafe), WMP_errFail);
#else
        FailIf(ICERR_OK != ImageStrDecInit(pWMII, &pID->WMP.wmiSCP, &pID->WMP.ctxSC, cbStride), WMP_errFail);
#endif

        if (pID->WMP.bHasAlpha) // planar alpha
            pID->WMP.wmiSCP.uAlphaMode = tempAlphaMode;
    }

Cleanup:

    if (WMP_errSuccess != err)
    {
        ERR_CODE errCode1 = (ERR_CODE)ImageStrDecTerm(pID->WMP.ctxSC);
    }

    return err;
}

//==================== External functions ===============

#ifdef WEB_CLIENT_SUPPORT
Void SaveDecoderState(CTXSTRCODEC ctxSC);
Void SetRestoreAfterFailure(CTXSTRCODEC ctxSC);
Bool NeedRestoreAfterFailure(CTXSTRCODEC ctxSC);
Void RestoreDecoderState(CTXSTRCODEC ctxSC);
#endif

//=======================================================

// pb and cbStride should be the same as cbStride passed to JXR_BeginDecodingMBRows()
ERR JXR_DecodeNextMBRow(
    PKImageDecode *pID, 
    U8 *pb, 
    size_t cbStride, 
    size_t *pNumLinesDecoded, Bool *pFinished
    )
{
    ERR err = WMP_errSuccess;
    Bool finished = FALSE;

    U32 linesPerMBRow;
    CWMImageBufferInfo wmiBI = { 0 };
    U32 i, cMBRow;
    U8 tempAlphaMode = 0;
    const CWMImageInfo *pWMII = &pID->WMP.wmiI;
    size_t lowMemAdj;
    size_t currMBRow = pID->WMP.DecoderCurrMBRow;
    size_t cLinesCropped = pID->WMP.cLinesCropped;

    *pNumLinesDecoded = 0;
    *pFinished = FALSE;

    // note the following implementation can't handle fractional linesPerMBRow limiting
    // us to >= 1/256 thumbnail which is unfortunate, but all the PS plugin needs is 1/256
    // and I didn't care to get into floating point or a bunch of conditional tests or
    // other rewrite for a case not needed nor tested by PS plugin.  sorry.
    linesPerMBRow = 16 / pWMII->cThumbnailScale;
    wmiBI.cLine = linesPerMBRow; // we always pass a buffer of at least linesPerMBRow * cbStride size

    if ((!pID->WMP.bHasAlpha) || (pID->WMP.wmiSCP.uAlphaMode != 1))
    {
        if (pID->WMP.bHasAlpha) //planar alpha
        {
            tempAlphaMode = pID->WMP.wmiSCP.uAlphaMode;
            pID->WMP.wmiSCP.uAlphaMode = 0;
        }

        //@pID->WMP.wmiSCP.fMeasurePerf = TRUE;

        // Re-entrant mode incurs 1 MBR delay, so to get 0th MBR, we have to ask for 1st MBR
        cMBRow = 0 == currMBRow ? 2 : currMBRow + 1;

        // O_FLIPV and O_FLIPVH, outputMBRow() and other output functions try to write to
        // the bottom of full-ROI buffer. Adjust the buffer pointer to compensate.
        lowMemAdj = 0 == currMBRow ? 0 : ((currMBRow - 1) * linesPerMBRow - pID->WMP.cLinesCropped) * cbStride;

        if (O_FLIPV == pWMII->oOrientation || O_FLIPVH == pWMII->oOrientation)
            lowMemAdj = (pWMII->cROIHeight - linesPerMBRow) * cbStride - lowMemAdj;

        wmiBI.pv = pb - lowMemAdj;
        wmiBI.cbStride = cbStride;

#ifdef WEB_CLIENT_SUPPORT
        if (NeedRestoreAfterFailure(pID->WMP.ctxSC))
            RestoreDecoderState(pID->WMP.ctxSC);
        else
            SaveDecoderState(pID->WMP.ctxSC);
#endif

        for (i = (U32)currMBRow; i < cMBRow; i++)
        {
            size_t cLinesDecoded;
            wmiBI.uiFirstMBRow = i;
            wmiBI.uiLastMBRow = i;

            if (ICERR_OK != ImageStrDecDecode(pID->WMP.ctxSC, &wmiBI, &cLinesDecoded))
            {
#ifdef WEB_CLIENT_SUPPORT
                SetRestoreAfterFailure(pID->WMP.ctxSC);
#endif
                err = WMP_errFail;
                goto Cleanup;
            }

            pID->WMP.cLinesDecoded += cLinesDecoded; // total number of lines decoded for current ROI
            *pNumLinesDecoded += cLinesDecoded; // number of lines decoded in this call

            if (FALSE == pID->WMP.fFirstNonZeroDecode && cLinesDecoded > 0)
            {
                pID->WMP.fFirstNonZeroDecode = TRUE;
                cLinesCropped += linesPerMBRow - cLinesDecoded;
            }

            if (0 == cLinesDecoded && i > 0)
            {
                ++cMBRow;
                cLinesCropped += linesPerMBRow;
            }
        }

        // If we're past the top of the image, then we're done
        if (linesPerMBRow * (cMBRow - 1) >= pWMII->cROIHeight + cLinesCropped)
            *pFinished = TRUE;

        pID->WMP.DecoderCurrMBRow = cMBRow; // Set to next possible MBRow that is decodable
        pID->WMP.cLinesCropped = cLinesCropped;

        if (pID->WMP.bHasAlpha) // planar alpha
            pID->WMP.wmiSCP.uAlphaMode = tempAlphaMode;
    }

Cleanup:

    if (WMP_errSuccess != err)
    {
        if (pID->WMP.bHasAlpha) // planar alpha
            pID->WMP.wmiSCP.uAlphaMode = tempAlphaMode;
    }

    return err;
}

void JXR_EndDecodingMBRows(
    PKImageDecode *pID)
{
    ERR_CODE errCode1 = (ERR_CODE)ImageStrDecTerm(pID->WMP.ctxSC);
    pID->WMP.ctxSC = NULL;
    pID->WMP.DecoderCurrMBRow = 0;
}

// Begin decoding planar alpha channel.
// Output row stride will remain the same for all subsequent calls to JXR_DecodeNextMBRow_Alpha()
// failSafe = TRUE causes the coding context to be saved before every attempt to decode a row of macroblocks
// when an image is not fully available (like in Web clients that try to decode as much of a partially downloaded image as possible).
// If an attempt was not successful (because there was not enough data), the coding context (which includes Huffmann tables
// and other data that gets modified during the decoding) is restored, and a new attempt to decode is made after more data is available.
ERR JXR_BeginDecodingMBRows_Alpha(
    PKImageDecode *pID, 
    const PKRect *pRect, 
    U8 *pb, 
    size_t cbStride, 
    Bool fullROIBuffer 
#ifdef WEB_CLIENT_SUPPORT
    , Bool failSafe
#endif
    )
{
    ERR err = WMP_errSuccess;
    struct WMPStream *pWS = pID->pStream;
    CWMImageInfo *pWMII = &pID->WMP.wmiI_Alpha;
    size_t pos;
    PKRect roi;

    if (!pID->WMP.bHasAlpha)
        return WMP_errDataNotAvailable;

    // In "Low Memory mode" (i.e. when the decode buffer can fit only one macroblock row), 
    // we don't have full frame buffer. We therefore cannot rotate the image.
    // We can flip H, V and HV, but no rotations.
    FailIf(!fullROIBuffer && pWMII->oOrientation >= O_RCW, WMP_errFail);

    // Normally, dimensions for alpha plane are set in the caller of this function.
    // If not set, get them from the tags in the file header
    if (0 == pWMII->cWidth || 0 == pWMII->cHeight)
        pWMII->cWidth = pID->uWidth, pWMII->cHeight = pID->uWidth;

    // Set Region of Interest
    if (NULL == pRect) // the user did not specify ROI
        roi.X = roi.Y = roi.Width = roi.Height = 0; // a correct ROI will be set in WMPhotoValidate()
    else
    {
        CalcThumbnailSize(pWMII);
        TranslateROI(pRect, &roi, pWMII->cThumbnailWidth, pWMII->cThumbnailHeight, pWMII->oOrientation);
    }

    pWMII->cROILeftX = roi.X;
    pWMII->cROITopY = roi.Y;
    pWMII->cROIWidth = roi.Width;
    pWMII->cROIHeight = roi.Height;

    // !!! Note: pID->WMP.wmiI_Alpha.cBitsPerUnit should be set in the caller of this function
    // because planar alpha can be decoded separately from main image plane, and into a different buffer !!!

    pID->WMP.wmiSCP_Alpha.pWStream = pWS;
    pID->WMP.DecoderCurrMBRow_Alpha = 0;
    pID->WMP.cLinesCropped_Alpha = 0;
    pID->WMP.fFirstNonZeroDecode_Alpha = FALSE;

    // Set the fPaddedUserBuffer if the following conditions are met
    if (0 == ((size_t)pb % 128) &&    // Frame buffer is aligned to 128-byte boundary
        0 == (cbStride % 128))        // Stride is a multiple of 128 bytes
    {
        pWMII->fPaddedUserBuffer = TRUE;
        // Note that there are additional conditions in strdec_x86.c's strDecOpt
        // which could prevent optimization from being engaged
    }

    //@pID->WMP.wmiSCP_Alpha.fMeasurePerf = TRUE;

    // The stream does not necessarily begin where the image file begins.
    // We should not expect that before calling this function the right stream position was set.
    // The caller should rather set pID->offStart. Calculate and set the stream position here.
    assert(pID->WMP.wmiDEMisc.uAlphaOffset >= pID->offStart);
    pos = pID->WMP.wmiDEMisc.uAlphaOffset - pID->offStart;
    pID->WMP.wmiSCP_Alpha.uStreamImageOffset = pos;

    Call(pWS->SetPos(pWS, pos));

    switch (pID->WMP.wmiI.bdBitDepth) // bit depth of main image plane (not alpha plane)
    {
        case BD_8:
            pWMII->cLeadingPadding = (pWMII->cBitsPerUnit >> 3) - 1;
            break;

        case BD_16:
        case BD_16S:
        case BD_16F:
            pWMII->cLeadingPadding = (pWMII->cBitsPerUnit >> 3) / sizeof(U16) - 1;
            break;

        case BD_32:
        case BD_32S:
        case BD_32F:
            pWMII->cLeadingPadding = (pWMII->cBitsPerUnit >> 3) / sizeof(float) - 1;
            break;

        case BD_5:
        case BD_10:
        case BD_565:
        default:
            break;
    }

#ifdef WEB_CLIENT_SUPPORT
    FailIf(ICERR_OK != ImageStrDecInit(pWMII, &pID->WMP.wmiSCP_Alpha, &pID->WMP.ctxSC_Alpha, cbStride, failSafe), WMP_errFail);
#else
    FailIf(ICERR_OK != ImageStrDecInit(pWMII, &pID->WMP.wmiSCP_Alpha, &pID->WMP.ctxSC_Alpha, cbStride), WMP_errFail);
#endif

Cleanup:

    if (WMP_errSuccess != err)
    {
        ERR_CODE errCode = (ERR_CODE)ImageStrDecTerm(pID->WMP.ctxSC_Alpha);
    }

    return err;
}

// cbStride should be exactly the same as cbStride passed to JXR_BeginDecodingMBRows_Alpha()
Bool JXR_DecodeNextMBRow_Alpha(
    PKImageDecode *pID, 
    U8 *pb, 
    size_t cbStride, 
    size_t *pNumLinesDecoded, Bool *pFinished
    )
{
    ERR err = WMP_errSuccess;
    U32 linesPerMBRow;
    CWMImageBufferInfo wmiBI;
    U32 i, cMBRow;
    const CWMImageInfo *pWMII = &pID->WMP.wmiI_Alpha;
    size_t lowMemAdj;
    size_t currMBRow = pID->WMP.DecoderCurrMBRow_Alpha;
    size_t cLinesCropped = pID->WMP.cLinesCropped_Alpha;

    *pNumLinesDecoded = 0;
    *pFinished = FALSE;

    // note the following implementation can't handle fractional linesPerMBRow limiting
    // us to >= 1/256 thumbnail which is unfortunate, but all the PS plugin needs is 1/256
    // and I didn't care to get into floating point or a bunch of conditional tests or
    // other rewrite for a case not needed nor tested by PS plugin.
    linesPerMBRow = 16 / pWMII->cThumbnailScale;
    wmiBI.cLine = linesPerMBRow; // we always pass a buffer of at least linesPerMBRow * cbStride size to this function

    // Re-entrant mode incurs 1 MBR delay, so to get 0th MBR, we have to ask for 1st MBR
    cMBRow = 0 == currMBRow ? 2 : currMBRow + 1;

    // O_FLIPV and O_FLIPVH, outputMBRow() and other output functions try to write to
    // the bottom of full-ROI buffer. Adjust the buffer pointer to compensate.
    lowMemAdj = 0 == currMBRow ? 0 : ((currMBRow - 1) * linesPerMBRow - pID->WMP.cLinesCropped_Alpha) * cbStride;

    if (O_FLIPV == pWMII->oOrientation || O_FLIPVH == pWMII->oOrientation)
        lowMemAdj = (pWMII->cROIHeight - linesPerMBRow) * cbStride - lowMemAdj;

    wmiBI.pv = pb - lowMemAdj;
    wmiBI.cbStride = cbStride;

#ifdef WEB_CLIENT_SUPPORT
    if (NeedRestoreAfterFailure(pID->WMP.ctxSC_Alpha))
        RestoreDecoderState(pID->WMP.ctxSC_Alpha);
    else
        SaveDecoderState(pID->WMP.ctxSC_Alpha);
#endif

    for (i = (U32)currMBRow; i < cMBRow; i++)
    {
        size_t cLinesDecoded;
        wmiBI.uiFirstMBRow = wmiBI.uiLastMBRow = i;

        if (ICERR_OK != ImageStrDecDecode(pID->WMP.ctxSC_Alpha, &wmiBI, &cLinesDecoded))
        {
#ifdef WEB_CLIENT_SUPPORT
            SetRestoreAfterFailure(pID->WMP.ctxSC_Alpha);
#endif
            err = WMP_errFail;
            goto Cleanup;
        }

        pID->WMP.cLinesDecoded = cLinesDecoded; // total number of lines decoded for current ROI
        *pNumLinesDecoded += cLinesDecoded; // number of lines decoded in this call

        if (FALSE == pID->WMP.fFirstNonZeroDecode_Alpha && cLinesDecoded > 0)
        {
            pID->WMP.fFirstNonZeroDecode_Alpha = TRUE;
            cLinesCropped += linesPerMBRow - cLinesDecoded;
        }

        if (0 == cLinesDecoded && i > 0)
        {
            ++cMBRow;
            cLinesCropped += linesPerMBRow;
        }
    }

    // If we're past the bottom of the image, then we're done
    if (linesPerMBRow * (cMBRow - 1) >= pWMII->cROIHeight + cLinesCropped)
        *pFinished = TRUE;

    pID->WMP.DecoderCurrMBRow_Alpha = cMBRow; // Set to next possible MBRow that is decodable
    pID->WMP.cLinesCropped_Alpha = cLinesCropped;

Cleanup:

    return err;
}

void JXR_EndDecodingMBRows_Alpha(
    PKImageDecode *pID)
{
    ERR_CODE errCode = (ERR_CODE)ImageStrDecTerm(pID->WMP.ctxSC_Alpha);
    pID->WMP.ctxSC_Alpha = NULL;
    pID->WMP.DecoderCurrMBRow_Alpha = 0;
}

// This function can be used only for decoding images that are fully available (including alpha, if alpha needs to be decoded).
// The decoding is performed into a buffer that is big enough to contain the ROI passed to the function 
// (i.e. it is at least pRect->Height * cbStride bytes in size)
//  pRect is Region if Interest (ROI). It can be NULL - in this case, the entire frame will be decoded.
ERR PKImageDecode_Copy_WMP(
    PKImageDecode *pID,
    const PKRect *pRect,
    U8 *pb,
    U32 cbStride)
{
    ERR err = WMP_errSuccess;
    Bool decodeAlpha = pID->WMP.wmiSCP.uAlphaMode != 0;
    Bool hasPlanarAlpha = pID->WMP.bHasAlpha != 0;
    Bool mainPlaneFinished = TRUE, alphaFinished = TRUE;
    Bool upsideDown = O_FLIPV == pID->WMP.wmiI.oOrientation || O_FLIPVH == pID->WMP.wmiI.oOrientation;
    I32 stepStride;
    U8 *pStart, *pRow;
    size_t numLinesDecoded;

#ifdef WEB_CLIENT_SUPPORT
    Call(JXR_BeginDecodingMBRows(pID, pRect, pb, cbStride, TRUE, FALSE)); // decoding into a full-ROI buffer, not fail-safe
#else
    Call(JXR_BeginDecodingMBRows(pID, pRect, pb, cbStride, TRUE)); // decoding into a full-ROI buffer
#endif

    stepStride = cbStride;

    if (upsideDown) // the image is flipped vertically
    {
        U32 linesPerMBRow = 16 / pID->WMP.wmiI.cThumbnailScale;
        pStart = pb + (pRect->Height - linesPerMBRow) * cbStride;
        stepStride = -stepStride;
    }
    else
        pStart = pb;

    //////////////////////
    // Decode main image plane

    mainPlaneFinished = FALSE;

    for (pRow = pStart; ; pRow += numLinesDecoded * stepStride)
    {
        Bool finished;
        Call(JXR_DecodeNextMBRow(pID, pRow, cbStride, &numLinesDecoded, &finished));

        if (0 == numLinesDecoded)
        {
            err = WMP_errFail; // should not happen
            break;
        }

        if (finished)
            break;
    }

    mainPlaneFinished = TRUE;
    JXR_EndDecodingMBRows(pID); // free the resources

    //////////////////////
    // Decode alpha plane

    if (decodeAlpha && hasPlanarAlpha)
    {
        CWMImageInfo *pWMII = &pID->WMP.wmiI_Alpha;
        struct WMPStream *pWS = pID->pStream;
        size_t pos;

        // The stream does not necessarily begin where the image file begins.
        // We should not expect that before calling this function the right stream position was set.
        // The caller should rather set pID->offStart. Calculate and set the stream position here.
        assert(pID->WMP.wmiDEMisc.uAlphaOffset >= pID->offStart);
        pos = pID->WMP.wmiDEMisc.uAlphaOffset - pID->offStart;
        pID->WMP.wmiSCP_Alpha.uStreamImageOffset = pos;

        Call(pWS->SetPos(pWS, pos));

        pID->WMP.wmiSCP_Alpha.pWStream = pWS;
        ImageStrDecGetInfo(pWMII, &pID->WMP.wmiSCP_Alpha);

        // We decode alpha plane into the same buffer as main image plane, so 
        // set the same cBitsPerUnit in WMP.wmiI_Alpha as in WMP.wmiI
        pWMII->cBitsPerUnit = pID->WMP.wmiI.cBitsPerUnit;

        // override orientation (it could have been set manually before decoding main image plane)
        pWMII->oOrientation = pID->WMP.wmiI.oOrientation;
        pWMII->cThumbnailScale = pID->WMP.wmiI.cThumbnailScale; // use the same thumbnail scale

        // We also decode alpha plane from the same stream as main image plane.
        // Thumbnail size will be calculated in JXR_BeginDecodingMBRows_Alpha()
#ifdef WEB_CLIENT_SUPPORT
        Call(JXR_BeginDecodingMBRows_Alpha(pID, pRect, pb, cbStride, TRUE, FALSE));
#else
        Call(JXR_BeginDecodingMBRows_Alpha(pID, pRect, pb, cbStride, TRUE));
#endif

        alphaFinished = FALSE;
        stepStride = cbStride;

        if (upsideDown) // the image is flipped vertically
            stepStride = -stepStride;

        for (pRow = pStart; ; pRow += numLinesDecoded * stepStride)
        {
            Bool finished;
            Call(JXR_DecodeNextMBRow_Alpha(pID, pRow, cbStride, &numLinesDecoded, &finished));

            if (0 == numLinesDecoded)
            {
                err = WMP_errFail; // should not happen
                break;
            }

            if (finished)
                break;
        }

        alphaFinished = TRUE;
        JXR_EndDecodingMBRows_Alpha(pID);
    }

Cleanup:

    if (!mainPlaneFinished)
        JXR_EndDecodingMBRows(pID);

    if (!alphaFinished)
        JXR_EndDecodingMBRows_Alpha(pID);

    return err;
}

#else

//////////////////////////////////////////////////////////////////////////////////////////////////
//
// This function can be used only for decoding images that are fully available (including alpha, if alpha needs to be decoded).
// The decoding is performed into a buffer that is big enough to contain the ROI passed to the function 
// (i.e. it is at least pRect->Height * cbStride bytes in size)
//  pRect is Region if Interest (ROI). It can be NULL - in this case, the entire frame will be decoded.
ERR PKImageDecode_Copy_WMP(
    PKImageDecode* pID,
    const PKRect* pRect,
    U8* pb,
    U32 cbStride)
{
    ERR err = WMP_errSuccess;
    U32 linesPerMBRow;
    CWMImageBufferInfo wmiBI = { 0 };
    struct WMPStream* pWS = pID->pStream;
    CWMImageInfo *pWMII = &pID->WMP.wmiI;
    size_t numLinesCropped;
    size_t pos;
    PKRect roi;

    U8 tempAlphaMode = 0;
    wmiBI.pv = pb;
    wmiBI.cLine = pRect->Height;
    wmiBI.cbStride = cbStride;

    // Set Region of Interest
    if (NULL == pRect) // the user did not specify ROI
        roi.X = roi.Y = roi.Width = roi.Height = 0; // a correct ROI will be set in WMPhotoValidate()
    else
    {
        CalcThumbnailSize(pWMII);
        TranslateROI(pRect, &roi, pWMII->cThumbnailWidth, pWMII->cThumbnailHeight, pWMII->oOrientation);
    }

    pWMII->cROILeftX = roi.X;
    pWMII->cROITopY = roi.Y;
    pWMII->cROIWidth = roi.Width;
    pWMII->cROIHeight = roi.Height;

    // note the following implementation can't handle fractional linesperMBRow limiting
    // us to >= 1/256 thumbnail which is unfortunate, but all the PS plugin needs is 1/256
    // and I didn't care to get into floating point or a bunch of conditional tests or
    // other rewrite for a case not needed nor tested by PS plugin.  sorry.
    linesPerMBRow = 16 / pWMII->cThumbnailScale;

    // Set the fPaddedUserBuffer if the following conditions are met
    if (0 == ((size_t)pb % 128) &&    // Frame buffer is aligned to 128-byte boundary
        0 == (cbStride % 128))              // Stride is a multiple of 128 bytes
    {
        pID->WMP.wmiI.fPaddedUserBuffer = TRUE;
        // Note that there are additional conditions in strdec_x86.c's strDecOpt
        // which could prevent optimization from being engaged
    }

    if((!pID->WMP.bHasAlpha) || (pID->WMP.wmiSCP.uAlphaMode != 1))
    {
        if(pID->WMP.bHasAlpha) //planar alpha
        {
            tempAlphaMode = pID->WMP.wmiSCP.uAlphaMode;
            pID->WMP.wmiSCP.uAlphaMode = 0;
        }

        // The stream does not necessarily begin where the image file begins.
        // We should not expect that before calling this function the right stream position was set.
        // The caller should rather set pID->offStart. Calculate and set the stream position here.
        assert(pID->WMP.wmiDEMisc.uImageOffset >= pID->offStart);
        pos = pID->WMP.wmiDEMisc.uImageOffset - pID->offStart;
        pID->WMP.wmiSCP.uStreamImageOffset = pos;
        Call(pID->WMP.wmiSCP.pWStream->SetPos(pID->WMP.wmiSCP.pWStream, pos));

        pID->WMP.wmiSCP.fMeasurePerf = TRUE;
        FailIf(ICERR_OK != ImageStrDecInit(pWMII, &pID->WMP.wmiSCP, &pID->WMP.ctxSC, cbStride), WMP_errFail);
        FailIf(ICERR_OK != ImageStrDecDecode(pID->WMP.ctxSC, &wmiBI, &numLinesCropped), WMP_errFail);
        FailIf(ICERR_OK != ImageStrDecTerm(pID->WMP.ctxSC), WMP_errFail);

        pID->WMP.cLinesCropped = numLinesCropped;

        if(pID->WMP.bHasAlpha)//planar alpha
        {
            pID->WMP.wmiSCP.uAlphaMode = tempAlphaMode;
        }
    }

    if(pID->WMP.bHasAlpha && pID->WMP.wmiSCP.uAlphaMode != 0)
    {
        pWMII = &pID->WMP.wmiI_Alpha;

        // The stream does not necessarily begin where the image file begins.
        // We should not expect that before calling this function the right stream position was set.
        // The caller should rather set pID->offStart. Calculate and set the stream position here.
        assert(pID->WMP.wmiDEMisc.uAlphaOffset >= pID->offStart);
        pID->WMP.wmiSCP_Alpha.pWStream = pWS;
        pos = pID->WMP.wmiDEMisc.uAlphaOffset - pID->offStart;
        pID->WMP.wmiSCP_Alpha.uStreamImageOffset = pos;

        Call(pWS->SetPos(pWS, pos));

        ImageStrDecGetInfo(pWMII, &pID->WMP.wmiSCP_Alpha);

        // We are decoding alpha plane into the same buffer as main image plane
        pWMII->cBitsPerUnit = pID->WMP.wmiI.cBitsPerUnit;

        // override orientation (it could have been set manually when decoding main image plane)
        pWMII->oOrientation = pID->WMP.wmiI.oOrientation;

        pWMII->cThumbnailScale = pID->WMP.wmiI.cThumbnailScale;

#if 0
        CalcThumbnailSize(pWMII); // we could instead set the same thummbnail size as in main image plane
#else
        pWMII->cThumbnailWidth = pID->WMP.wmiI.cThumbnailWidth;
        pWMII->cThumbnailHeight = pID->WMP.wmiI.cThumbnailHeight;
#endif

        // Set Region of Interest
        pWMII->cROILeftX = roi.X;
        pWMII->cROITopY = roi.Y;
        pWMII->cROIWidth = roi.Width;
        pWMII->cROIHeight = roi.Height;

        switch (pID->WMP.wmiI.bdBitDepth) // main image plane's bit depth
        {
            case BD_8:
                pWMII->cLeadingPadding = (pID->WMP.wmiI.cBitsPerUnit >> 3) - 1;
                break;

            case BD_16:
            case BD_16S:
            case BD_16F:
                pWMII->cLeadingPadding = (pID->WMP.wmiI.cBitsPerUnit >> 3) / sizeof(U16) - 1;
                break;

            case BD_32:
            case BD_32S:
            case BD_32F:
                pWMII->cLeadingPadding = (pID->WMP.wmiI.cBitsPerUnit >> 3) / sizeof(float) - 1;
                break;

            case BD_5:
            case BD_10:
            case BD_565:
            default:
                break;
        }

        pID->WMP.wmiSCP_Alpha.fMeasurePerf = TRUE;

        FailIf(ICERR_OK != ImageStrDecInit(&pID->WMP.wmiI_Alpha, &pID->WMP.wmiSCP_Alpha, &pID->WMP.ctxSC_Alpha, cbStride), WMP_errFail);
        FailIf(ICERR_OK != ImageStrDecDecode(pID->WMP.ctxSC_Alpha, &wmiBI, &numLinesCropped), WMP_errFail);
        FailIf(ICERR_OK != ImageStrDecTerm(pID->WMP.ctxSC_Alpha), WMP_errFail);
    }

    pID->WMP.cLinesCropped_Alpha = numLinesCropped;

Cleanup:
    return err;
}

#endif

ERR PKImageDecode_GetMetadata_WMP(PKImageDecode *pID, U32 uOffset, U32 uByteCount, U8 *pbGot, U32 *pcbGot)
{
    ERR err = WMP_errSuccess;

    if (pbGot && uOffset)
    {
        struct WMPStream* pWS = pID->pStream;
        size_t iCurrPos;

        FailIf(*pcbGot < uByteCount, WMP_errBufferOverflow);
        Call(pWS->GetPos(pWS, &iCurrPos));
        Call(pWS->SetPos(pWS, uOffset));
        Call(pWS->Read(pWS, pbGot, uByteCount));
        Call(pWS->SetPos(pWS, iCurrPos));
    }

Cleanup:
    if (Failed(err))
        *pcbGot = 0;
    else
        *pcbGot = uByteCount;

    return err;
}



ERR PKImageDecode_GetColorContext_WMP(PKImageDecode *pID, U8 *pbColorContext, U32 *pcbColorContext)
{
    return PKImageDecode_GetMetadata_WMP(pID, pID->WMP.wmiDEMisc.uColorProfileOffset,
        pID->WMP.wmiDEMisc.uColorProfileByteCount, pbColorContext, pcbColorContext);
}



ERR PKImageDecode_GetDescriptiveMetadata_WMP(PKImageDecode *pID, DESCRIPTIVEMETADATA *pDescMetadata)
{
    ERR err = WMP_errSuccess;
    *pDescMetadata = pID->WMP.sDescMetadata;
    return err;
}


ERR PKImageDecode_Release_WMP(PKImageDecode** ppID)
{
    ERR             err = WMP_errSuccess;
    PKImageDecode  *pID;

    if (NULL == ppID)
        goto Cleanup;

    pID = *ppID;

    // Free descriptive metadata
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarImageDescription);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarCameraMake);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarCameraModel);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarSoftware);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarDateTime);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarArtist);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarCopyright);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarRatingStars);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarRatingValue);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarCaption);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarDocumentName);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarPageName);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarPageNumber);
    FreeDescMetadata(&pID->WMP.sDescMetadata.pvarHostComputer);

    // Release base class
    Call(PKImageDecode_Release(ppID));

Cleanup:
    return err;
}



ERR PKImageDecode_Create_WMP(PKImageDecode** ppID)
{
    ERR err = WMP_errSuccess;
    PKImageDecode* pID = NULL;

    Call(PKImageDecode_Create(ppID));

    pID = *ppID;
    pID->Initialize = PKImageDecode_Initialize_WMP;
    pID->GetSize = PKImageDecode_GetSize_WMP;
    pID->GetRawStream = PKImageDecode_GetRawStream_WMP;
    pID->Copy = PKImageDecode_Copy_WMP;

    pID->GetColorContext = PKImageDecode_GetColorContext_WMP;
    pID->GetDescriptiveMetadata = PKImageDecode_GetDescriptiveMetadata_WMP;
    pID->Release = PKImageDecode_Release_WMP;

Cleanup:
    return err;
}
