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
/******************************************************************************

Module Name:
    decode.c 
    
Abstract:    
    Defines the entry point for the console application.

Author:

Revision History:
*******************************************************************************/
#include "strcodec.h"
#include "decode.h"

#ifdef MEM_TRACE
#define TRACE_MALLOC    1
#define TRACE_NEW       0
#define TRACE_HEAP      0
#include "memtrace.h"
#endif

/******************************************************************
Free Adaptive Huffman Table
******************************************************************/
static Void CleanAH(CAdaptiveHuffman **ppAdHuff)
{
    CAdaptiveHuffman *pAdHuff;
    
    if (NULL != ppAdHuff) {
        pAdHuff = *ppAdHuff;
        if (NULL != pAdHuff) {
            free(pAdHuff);
        }
        *ppAdHuff = NULL;
    }
}

static Void CleanAHDec(CCodingContext * pSC)
{
    Int kk;

    for (kk = 0; kk < NUMVLCTABLES; kk++) {
        CleanAH(&(pSC->m_pAHexpt[kk]));
    }
    CleanAH(&(pSC->m_pAdaptHuffCBPCY));
    CleanAH(&(pSC->m_pAdaptHuffCBPCY1));
}

/*************************************************************************
    Initialize an adaptive huffman table
*************************************************************************/
static Int InitializeAH(CAdaptiveHuffman **ppAdHuff, Int iSym)
{
    Int iMemStatus = 0;

    CAdaptiveHuffman *pAdHuff = Allocate(iSym, DECODER);
    if (pAdHuff == NULL) {
        iMemStatus = -1;    // out of memory
        goto ErrorExit;
    }

    //Adapt(pAdHuff, bFixedTables);
    //InitHuffman(pAdHuff->m_pHuffman);
    //if (ICERR_OK != initHuff(pAdHuff->m_pHuffman, 1, pAdHuff->m_pTable, NULL)) {
    //    goto ErrorExit;
    //}
    *ppAdHuff = pAdHuff;
    return ICERR_OK;

ErrorExit:
    if (pAdHuff) {
        free(pAdHuff);
    }
    *ppAdHuff = NULL;
    if (-1 == iMemStatus) {
        printf("Insufficient memory to init decoder.\n");
    }
    return ICERR_ERROR;
}


/*************************************************************************
    Context allocation
*************************************************************************/
CCodingContext * AllocateCodingContextsDec(CWMImageStrCodec *pSC, Int iNumContexts)
{
    Int i, iCBPSize, k;
    static const Int aAlphabet[] = {5,4,8,7,7,  12,6,6,12,6,6,7,7,  12,6,6,12,6,6,7,7};
    CCodingContext *pCodingContexts;

    if (iNumContexts > MAX_TILES || iNumContexts < 1)  // only between 1 and MAX_TILES allowed
        return NULL;

    if (NULL == pSC)
        return NULL;

    pCodingContexts = (CCodingContext *)malloc(iNumContexts * sizeof(CCodingContext));

    if (NULL == pCodingContexts)
    {
        //@pSC->cNumCodingContext = 0;
        return NULL;
    }

    memset(pCodingContexts, 0, iNumContexts * sizeof(CCodingContext));

    //#pSC->cNumCodingContext = iNumContexts;

    iCBPSize = (pSC->m_param.cfColorFormat == Y_ONLY || pSC->m_param.cfColorFormat == NCOMPONENT
        || pSC->m_param.cfColorFormat == CMYK) ? 5 : 9;

    // allocate / initialize members
    for (i = 0; i < iNumContexts; i++)
    {
        CCodingContext *pContext = &(pCodingContexts[i]);

        // allocate adaptive Huffman encoder
        if (InitializeAH(&pContext->m_pAdaptHuffCBPCY, iCBPSize) != ICERR_OK)
        {
            FreeCodingContextsDec(pCodingContexts, iNumContexts);
            return NULL;
        }

        if (InitializeAH(&pContext->m_pAdaptHuffCBPCY1, 5) != ICERR_OK)
        {
            FreeCodingContextsDec(pCodingContexts, iNumContexts);
            return NULL;
        }

        for (k = 0; k < NUMVLCTABLES; k ++)
        {
            if (InitializeAH(&pContext->m_pAHexpt[k], aAlphabet[k]) != ICERR_OK)
            {
                FreeCodingContextsDec(pCodingContexts, iNumContexts);
                return NULL;
            }
        }

        ResetCodingContextDec(pContext);
    }

    return pCodingContexts;
}

/*************************************************************************
    Context reset on encoder
*************************************************************************/
Void ResetCodingContextDec(CCodingContext *pContext)
{
    Int k;
    /** set flags **/
    pContext->m_pAdaptHuffCBPCY->m_bInitialize = FALSE;
    pContext->m_pAdaptHuffCBPCY1->m_bInitialize = FALSE;
    for(k = 0; k < NUMVLCTABLES; k ++)
        pContext->m_pAHexpt[k]->m_bInitialize = FALSE;

    // reset VLC tables
    AdaptLowpassDec (pContext);
    AdaptHighpassDec (pContext);

    // reset zigzag patterns, totals
    InitZigzagScan(pContext);
    // reset bit reduction and cbp models
    ResetCodingContext(pContext);
}

#ifdef WEB_CLIENT_SUPPORT

//========================================================================
// Copying a decoding context (as part of saving and restoring the decoder state in Internet mode)
//========================================================================
static Void CopyCodingContextDec(const CCodingContext *pSrcContext, CCodingContext *pDestContext)
{
    //========== Save pointers ==============
    CAdaptiveHuffman *pAdaptHuffCBPCY = pDestContext->m_pAdaptHuffCBPCY;
    CAdaptiveHuffman *pAdaptHuffCBPCY1 = pDestContext->m_pAdaptHuffCBPCY1;
    CAdaptiveHuffman *pAHexpt[NUMVLCTABLES];
    Int k;
    memcpy(pAHexpt, pDestContext->m_pAHexpt, sizeof(pAHexpt));

    //========== Copy arrays ==================
    memcpy(pAdaptHuffCBPCY, pSrcContext->m_pAdaptHuffCBPCY, sizeof(CAdaptiveHuffman));
    memcpy(pAdaptHuffCBPCY1, pSrcContext->m_pAdaptHuffCBPCY1, sizeof(CAdaptiveHuffman));

    for (k = 0; k < NUMVLCTABLES; k++)
        memcpy(pAHexpt[k], pSrcContext->m_pAHexpt[k], sizeof(CAdaptiveHuffman));

    //============ Copy the structure ================
    memcpy(pDestContext, pSrcContext, sizeof(CCodingContext));

    //================ Restore pointers ============
    pDestContext->m_pAdaptHuffCBPCY = pAdaptHuffCBPCY;
    pDestContext->m_pAdaptHuffCBPCY1 = pAdaptHuffCBPCY1;
    memcpy(pDestContext->m_pAHexpt, pAHexpt, sizeof(pAHexpt));
}

Void BackupCodingContextsDec(CWMImageStrCodec *pSC)
{
    const CCodingContext *pSrcContexts = pSC->m_pCodingContext;
    CCodingContext *pDestContexts = pSC->m_pCodingContextBackup;
    Int iContexts = (Int)(pSC->cNumCodingContext);
    Int i;

    for (i = 0; i < iContexts; ++i)
        CopyCodingContextDec(&pSrcContexts[i], &pDestContexts[i]);
}

Void RestoreCodingContextsDec(CWMImageStrCodec *pSC)
{
    const CCodingContext *pSrcContexts = pSC->m_pCodingContextBackup;
    CCodingContext *pDestContexts = pSC->m_pCodingContext;
    Int iContexts = (Int)(pSC->cNumCodingContext);
    Int i;

    for (i = 0; i < iContexts; ++i)
        CopyCodingContextDec(&pSrcContexts[i], &pDestContexts[i]);
}

#endif // WEB_CLIENT_SUPPORT

/*************************************************************************
    Context deletion
*************************************************************************/
Void FreeCodingContextDec(CWMImageStrCodec *pSC)
{
    Int iContexts = (Int)(pSC->cNumCodingContext), i, k;

    if (iContexts > 0 && pSC->m_pCodingContext) {

        for (i = 0; i < iContexts; i++) {
            CCodingContext *pContext = &(pSC->m_pCodingContext[i]);
            CleanAH (&pContext->m_pAdaptHuffCBPCY);
            CleanAH (&pContext->m_pAdaptHuffCBPCY1);
            for (k = 0; k < NUMVLCTABLES; k++)
                CleanAH (&pContext->m_pAHexpt[k]);
        }
        free (pSC->m_pCodingContext);
    }
}

Void FreeCodingContextsDec(CCodingContext *pCodingContexts, Int iContexts)
{
    Int i, k;

    if (NULL == pCodingContexts)
        return;

    for (i = 0; i < iContexts; i++)
    {
        CCodingContext *pContext = &(pCodingContexts[i]);
        CleanAH(&pContext->m_pAdaptHuffCBPCY);
        CleanAH(&pContext->m_pAdaptHuffCBPCY1);

        for (k = 0; k < NUMVLCTABLES; k++)
            CleanAH(&pContext->m_pAHexpt[k]);
    }

    free(pCodingContexts);
}
