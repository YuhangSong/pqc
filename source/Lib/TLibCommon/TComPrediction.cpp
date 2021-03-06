/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2016, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     TComPrediction.cpp
    \brief    prediction class
*/

#include <memory.h>
#include "TComPrediction.h"
#include "TComPic.h"
#include "TComTU.h"
#include "TComTrQuant.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Tables
// ====================================================================================================================

const UChar TComPrediction::m_aucIntraFilter[MAX_NUM_CHANNEL_TYPE][MAX_INTRA_FILTER_DEPTHS] =
{
  { // Luma
    10, //4x4
    7, //8x8
    1, //16x16
    0, //32x32
    10, //64x64
  },
  { // Chroma
    10, //4xn
    7, //8xn
    1, //16xn
    0, //32xn
    10, //64xn
  }

};

// ====================================================================================================================
// Constructor / destructor / initialize
// ====================================================================================================================

TComPrediction::TComPrediction()
: m_truncBinBits(NULL)
, m_escapeNumBins(NULL)
, m_pLumaRecBuffer(0)
, m_iLumaRecStride(0)
{
  for(UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    for(UInt buf=0; buf<2; buf++)
    {
      m_piYuvExt[ch][buf] = NULL;
    }
  }
  m_paletteErrLimit = 3;
}

TComPrediction::~TComPrediction()
{
  destroy();
}

Void TComPrediction::destroy()
{
  for(UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    for(UInt buf=0; buf<NUM_PRED_BUF; buf++)
    {
      delete [] m_piYuvExt[ch][buf];
      m_piYuvExt[ch][buf] = NULL;
    }
  }

  for(UInt i=0; i<NUM_REF_PIC_LIST_01; i++)
  {
    m_acYuvPred[i].destroy();
  }

  m_cYuvPredTemp.destroy();

  if( m_pLumaRecBuffer )
  {
    delete [] m_pLumaRecBuffer;
    m_pLumaRecBuffer = 0;
  }
  m_iLumaRecStride = 0;

  for (UInt i = 0; i < LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS; i++)
  {
    for (UInt j = 0; j < LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS; j++)
    {
      m_filteredBlock[i][j].destroy();
    }
    m_filteredBlockTmp[i].destroy();
  }

  if (m_truncBinBits)
  {
    for (UInt i = 0; i < m_SymbolSize; i++)
    {
      if (m_truncBinBits[i])
      {
        delete[] m_truncBinBits[i];
        m_truncBinBits[i] = NULL;
      }
    }
    delete[] m_truncBinBits;
    m_truncBinBits = NULL;
  }

  if( m_escapeNumBins )
  {
    delete[] m_escapeNumBins;
    m_escapeNumBins = NULL;
  }
}

Void TComPrediction::initTempBuff(ChromaFormat chromaFormatIDC)
{
  // if it has been initialised before, but the chroma format has changed, release the memory and start again.
  if( m_piYuvExt[COMPONENT_Y][PRED_BUF_UNFILTERED] != NULL && m_cYuvPredTemp.getChromaFormat()!=chromaFormatIDC)
  {
    destroy();
  }

  if( m_piYuvExt[COMPONENT_Y][PRED_BUF_UNFILTERED] == NULL ) // check if first is null (in which case, nothing initialised yet)
  {
    Int extWidth  = MAX_CU_SIZE + 16;
    Int extHeight = MAX_CU_SIZE + 1;

    for (UInt i = 0; i < LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS; i++)
    {
      m_filteredBlockTmp[i].create(extWidth, extHeight + 7, chromaFormatIDC);
      for (UInt j = 0; j < LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS; j++)
      {
        m_filteredBlock[i][j].create(extWidth, extHeight, chromaFormatIDC);
      }
    }

    m_iYuvExtSize = (MAX_CU_SIZE*2+1) * (MAX_CU_SIZE*2+1);
    for(UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
    {
      for(UInt buf=0; buf<NUM_PRED_BUF; buf++)
      {
        m_piYuvExt[ch][buf] = new Pel[ m_iYuvExtSize ];
      }
    }

    // new structure
    for(UInt i=0; i<NUM_REF_PIC_LIST_01; i++)
    {
      m_acYuvPred[i] .create( MAX_CU_SIZE, MAX_CU_SIZE, chromaFormatIDC );
    }

    m_cYuvPredTemp.create( MAX_CU_SIZE, MAX_CU_SIZE, chromaFormatIDC );

    m_prevQP=-1;
  }


  if (m_iLumaRecStride != (MAX_CU_SIZE>>1) + 1)
  {
    m_iLumaRecStride =  (MAX_CU_SIZE>>1) + 1;
    if (!m_pLumaRecBuffer)
    {
      m_pLumaRecBuffer = new Pel[ m_iLumaRecStride * m_iLumaRecStride ];
    }
  }
}

Void TComPrediction::initTBCTable(UInt bitDepth)
{
  m_MaxSymbolSize= (1 << bitDepth) + 1; //num of uiSymbol, max = 256, so size 257
  m_SymbolSize = m_MaxSymbolSize - 1; // uiSymbol is in the range of [0, 255], size = 256
  
  m_truncBinBits = new UShort*[m_SymbolSize];
  for (UInt i = 0; i < m_SymbolSize; i++)
  {
    m_truncBinBits[i] = new UShort[m_MaxSymbolSize];
    memset(m_truncBinBits[i], 0, sizeof(UShort)*m_MaxSymbolSize);
  }

  for (UInt i = 0; i < m_MaxSymbolSize; i++)
  {
    for (UInt j = 0; j < i; j++)
    {
      m_truncBinBits[j][i] = getTruncBinBits(j, i);
    }
  }

  m_escapeNumBins = new UShort[m_SymbolSize];
  memset(m_escapeNumBins, 0, sizeof(UShort)*m_SymbolSize);

  for( UInt i = 0; i < m_SymbolSize; i++)
  {
    m_escapeNumBins[i] = getEpExGolombNumBins(i, 3);
  }
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

// Function for calculating DC value of the reference samples used in Intra prediction
//NOTE: Bit-Limit - 25-bit source
Pel TComPrediction::predIntraGetPredValDC( const Pel* pSrc, Int iSrcStride, UInt iWidth, UInt iHeight)
{
  assert(iWidth > 0 && iHeight > 0);
  Int iInd, iSum = 0;
  Pel pDcVal;

  for (iInd = 0;iInd < iWidth;iInd++)
  {
    iSum += pSrc[iInd-iSrcStride];
  }
  for (iInd = 0;iInd < iHeight;iInd++)
  {
    iSum += pSrc[iInd*iSrcStride-1];
  }

  pDcVal = (iSum + iWidth) / (iWidth + iHeight);

  return pDcVal;
}

// Function for deriving the angular Intra predictions

/** Function for deriving the simplified angular intra predictions.
 * \param bitDepth           bit depth
 * \param pSrc               pointer to reconstructed sample array
 * \param srcStride          the stride of the reconstructed sample array
 * \param pTrueDst           reference to pointer for the prediction sample array
 * \param dstStrideTrue      the stride of the prediction sample array
 * \param uiWidth            the width of the block
 * \param uiHeight           the height of the block
 * \param channelType        type of pel array (luma/chroma)
 * \param format             chroma format
 * \param dirMode            the intra prediction mode index
 * \param blkAboveAvailable  boolean indication if the block above is available
 * \param blkLeftAvailable   boolean indication if the block to the left is available
 * \param bEnableEdgeFilters indication whether to enable edge filters
 *
 * This function derives the prediction samples for the angular mode based on the prediction direction indicated by
 * the prediction mode index. The prediction direction is given by the displacement of the bottom row of the block and
 * the reference row above the block in the case of vertical prediction or displacement of the rightmost column
 * of the block and reference column left from the block in the case of the horizontal prediction. The displacement
 * is signalled at 1/32 pixel accuracy. When projection of the predicted pixel falls inbetween reference samples,
 * the predicted value for the pixel is linearly interpolated from the reference samples. All reference samples are taken
 * from the extended main reference.
 */
//NOTE: Bit-Limit - 25-bit source
Void TComPrediction::xPredIntraAng(       Int bitDepth,
                                    const Pel* pSrc,     Int srcStride,
                                          Pel* pTrueDst, Int dstStrideTrue,
                                          UInt uiWidth, UInt uiHeight, ChannelType channelType,
                                          UInt dirMode, const Bool bEnableEdgeFilters
                                  )
{
  Int width=Int(uiWidth);
  Int height=Int(uiHeight);

  // Map the mode index to main prediction direction and angle
  assert( dirMode != PLANAR_IDX ); //no planar
  const Bool modeDC        = dirMode==DC_IDX;

  // Do the DC prediction
  if (modeDC)
  {
    const Pel dcval = predIntraGetPredValDC(pSrc, srcStride, width, height);

    for (Int y=height;y>0;y--, pTrueDst+=dstStrideTrue)
    {
      for (Int x=0; x<width;) // width is always a multiple of 4.
      {
        pTrueDst[x++] = dcval;
      }
    }
  }
  else // Do angular predictions
  {
    const Bool       bIsModeVer         = (dirMode >= 18);
    const Int        intraPredAngleMode = (bIsModeVer) ? (Int)dirMode - VER_IDX :  -((Int)dirMode - HOR_IDX);
    const Int        absAngMode         = abs(intraPredAngleMode);
    const Int        signAng            = intraPredAngleMode < 0 ? -1 : 1;
    const Bool       edgeFilter         = bEnableEdgeFilters && isLuma(channelType) && (width <= MAXIMUM_INTRA_FILTERED_WIDTH) && (height <= MAXIMUM_INTRA_FILTERED_HEIGHT);

    // Set bitshifts and scale the angle parameter to block size
    static const Int angTable[9]    = {0,    2,    5,   9,  13,  17,  21,  26,  32};
    static const Int invAngTable[9] = {0, 4096, 1638, 910, 630, 482, 390, 315, 256}; // (256 * 32) / Angle
    Int invAngle                    = invAngTable[absAngMode];
    Int absAng                      = angTable[absAngMode];
    Int intraPredAngle              = signAng * absAng;

    Pel* refMain;
    Pel* refSide;

    Pel  refAbove[2*MAX_CU_SIZE+1];
    Pel  refLeft[2*MAX_CU_SIZE+1];

    // Initialize the Main and Left reference array.
    if (intraPredAngle < 0)
    {
      const Int refMainOffsetPreScale = (bIsModeVer ? height : width ) - 1;
      const Int refMainOffset         = height - 1;
      for (Int x=0;x<width+1;x++)
      {
        refAbove[x+refMainOffset] = pSrc[x-srcStride-1];
      }
      for (Int y=0;y<height+1;y++)
      {
        refLeft[y+refMainOffset] = pSrc[(y-1)*srcStride-1];
      }
      refMain = (bIsModeVer ? refAbove : refLeft)  + refMainOffset;
      refSide = (bIsModeVer ? refLeft  : refAbove) + refMainOffset;

      // Extend the Main reference to the left.
      Int invAngleSum    = 128;       // rounding for (shift by 8)
      for (Int k=-1; k>(refMainOffsetPreScale+1)*intraPredAngle>>5; k--)
      {
        invAngleSum += invAngle;
        refMain[k] = refSide[invAngleSum>>8];
      }
    }
    else
    {
      for (Int x=0;x<2*width+1;x++)
      {
        refAbove[x] = pSrc[x-srcStride-1];
      }
      for (Int y=0;y<2*height+1;y++)
      {
        refLeft[y] = pSrc[(y-1)*srcStride-1];
      }
      refMain = bIsModeVer ? refAbove : refLeft ;
      refSide = bIsModeVer ? refLeft  : refAbove;
    }

    // swap width/height if we are doing a horizontal mode:
    Pel tempArray[MAX_CU_SIZE*MAX_CU_SIZE];
    const Int dstStride = bIsModeVer ? dstStrideTrue : MAX_CU_SIZE;
    Pel *pDst = bIsModeVer ? pTrueDst : tempArray;
    if (!bIsModeVer)
    {
      std::swap(width, height);
    }

    if (intraPredAngle == 0)  // pure vertical or pure horizontal
    {
      for (Int y=0;y<height;y++)
      {
        for (Int x=0;x<width;x++)
        {
          pDst[y*dstStride+x] = refMain[x+1];
        }
      }

      if (edgeFilter)
      {
        for (Int y=0;y<height;y++)
        {
          pDst[y*dstStride] = Clip3 (0, ((1 << bitDepth) - 1), pDst[y*dstStride] + (( refSide[y+1] - refSide[0] ) >> 1) );
        }
      }
    }
    else
    {
      Pel *pDsty=pDst;

      for (Int y=0, deltaPos=intraPredAngle; y<height; y++, deltaPos+=intraPredAngle, pDsty+=dstStride)
      {
        const Int deltaInt   = deltaPos >> 5;
        const Int deltaFract = deltaPos & (32 - 1);

        if (deltaFract)
        {
          // Do linear filtering
          const Pel *pRM=refMain+deltaInt+1;
          Int lastRefMainPel=*pRM++;
          for (Int x=0;x<width;pRM++,x++)
          {
            Int thisRefMainPel=*pRM;
            pDsty[x+0] = (Pel) ( ((32-deltaFract)*lastRefMainPel + deltaFract*thisRefMainPel +16) >> 5 );
            lastRefMainPel=thisRefMainPel;
          }
        }
        else
        {
          // Just copy the integer samples
          for (Int x=0;x<width; x++)
          {
            pDsty[x] = refMain[x+deltaInt+1];
          }
        }
      }
    }

    // Flip the block if this is the horizontal mode
    if (!bIsModeVer)
    {
      for (Int y=0; y<height; y++)
      {
        for (Int x=0; x<width; x++)
        {
          pTrueDst[x*dstStrideTrue] = pDst[x];
        }
        pTrueDst++;
        pDst+=dstStride;
      }
    }
  }
}

Void TComPrediction::predIntraAng( const ComponentID compID, UInt uiDirMode, Pel* piOrg /* Will be null for decoding */, UInt uiOrgStride, Pel* piPred, UInt uiStride, TComTU &rTu, const Bool bUseFilteredPredSamples, const Bool bUseLosslessDPCM )
{
  const ChannelType    channelType = toChannelType(compID);
  const TComRectangle &rect        = rTu.getRect(isLuma(compID) ? COMPONENT_Y : COMPONENT_Cb);
  const Int            iWidth      = rect.width;
  const Int            iHeight     = rect.height;

  assert( g_aucConvertToBit[ iWidth ] >= 0 ); //   4x  4
  assert( g_aucConvertToBit[ iWidth ] <= 5 ); // 128x128
  //assert( iWidth == iHeight  );

        Pel *pDst = piPred;

  // get starting pixel in block
  const Int sw = (2 * iWidth + 1);

  if ( bUseLosslessDPCM )
  {
    const Pel *ptrSrc = getPredictorPtr( compID, false );
    // Sample Adaptive intra-Prediction (SAP)
    if (uiDirMode==HOR_IDX)
    {
      // left column filled with reference samples
      // remaining columns filled with piOrg data (if available).
      for(Int y=0; y<iHeight; y++)
      {
        piPred[y*uiStride+0] = ptrSrc[(y+1)*sw];
      }
      if (piOrg!=0)
      {
        piPred+=1; // miss off first column
        for(Int y=0; y<iHeight; y++, piPred+=uiStride, piOrg+=uiOrgStride)
        {
          memcpy(piPred, piOrg, (iWidth-1)*sizeof(Pel));
        }
      }
    }
    else // VER_IDX
    {
      // top row filled with reference samples
      // remaining rows filled with piOrd data (if available)
      for(Int x=0; x<iWidth; x++)
      {
        piPred[x] = ptrSrc[x+1];
      }
      if (piOrg!=0)
      {
        piPred+=uiStride; // miss off the first row
        for(Int y=1; y<iHeight; y++, piPred+=uiStride, piOrg+=uiOrgStride)
        {
          memcpy(piPred, piOrg, iWidth*sizeof(Pel));
        }
      }
    }
  }
  else
  {
    const Pel *ptrSrc = getPredictorPtr( compID, bUseFilteredPredSamples );

    if ( uiDirMode == PLANAR_IDX )
    {
      xPredIntraPlanar( ptrSrc+sw+1, sw, pDst, uiStride, iWidth, iHeight );
    }
    else
    {
      // Create the prediction
            TComDataCU *const pcCU              = rTu.getCU();
      const UInt              uiAbsPartIdx      = rTu.GetAbsPartIdxTU();
      const Bool              enableEdgeFilters = !(pcCU->isRDPCMEnabled(uiAbsPartIdx) && pcCU->getCUTransquantBypass(uiAbsPartIdx)) && !pcCU->getSlice()->getSPS()->getSpsScreenExtension().getDisableIntraBoundaryFilter();
#if O0043_BEST_EFFORT_DECODING
      const Int channelsBitDepthForPrediction = rTu.getCU()->getSlice()->getSPS()->getStreamBitDepth(channelType);
#else
      const Int channelsBitDepthForPrediction = rTu.getCU()->getSlice()->getSPS()->getBitDepth(channelType);
#endif
      xPredIntraAng( channelsBitDepthForPrediction, ptrSrc+sw+1, sw, pDst, uiStride, iWidth, iHeight, channelType, uiDirMode, enableEdgeFilters );

      if( uiDirMode == DC_IDX && !pcCU->getSlice()->getSPS()->getSpsScreenExtension().getDisableIntraBoundaryFilter() )
      {
        xDCPredFiltering( ptrSrc+sw+1, sw, pDst, uiStride, iWidth, iHeight, channelType );
      }
    }
  }

}

/** Check for identical motion in both motion vector direction of a bi-directional predicted CU
  * \returns true, if motion vectors and reference pictures match
 */
Bool TComPrediction::xCheckIdenticalMotion ( TComDataCU* pcCU, UInt PartAddr )
{
  if( pcCU->getSlice()->isInterB() && !pcCU->getSlice()->getPPS()->getWPBiPred() )
  {
    if( pcCU->getCUMvField(REF_PIC_LIST_0)->getRefIdx(PartAddr) >= 0 && pcCU->getCUMvField(REF_PIC_LIST_1)->getRefIdx(PartAddr) >= 0)
    {
      Int RefPOCL0 = pcCU->getSlice()->getRefPic(REF_PIC_LIST_0, pcCU->getCUMvField(REF_PIC_LIST_0)->getRefIdx(PartAddr))->getPOC();
      Int RefPOCL1 = pcCU->getSlice()->getRefPic(REF_PIC_LIST_1, pcCU->getCUMvField(REF_PIC_LIST_1)->getRefIdx(PartAddr))->getPOC();
      if(RefPOCL0 == RefPOCL1 && pcCU->getCUMvField(REF_PIC_LIST_0)->getMv(PartAddr) == pcCU->getCUMvField(REF_PIC_LIST_1)->getMv(PartAddr))
      {
        return true;
      }
    }
  }
  return false;
}

Void TComPrediction::motionCompensation ( TComDataCU* pcCU, TComYuv* pcYuvPred, RefPicList eRefPicList, Int iPartIdx )
{
  Int         iWidth;
  Int         iHeight;
  UInt        uiPartAddr;
  const TComSlice *pSlice    = pcCU->getSlice();
  const SliceType  sliceType = pSlice->getSliceType();
  const TComPPS   &pps       = *(pSlice->getPPS());

  if ( iPartIdx >= 0 )
  {
    pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iWidth, iHeight );
    if ( eRefPicList != REF_PIC_LIST_X )
    {
      if( (sliceType == P_SLICE && pps.getUseWP()) || (sliceType == B_SLICE && pps.getWPBiPred()))
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred, true );
        xWeightedPredictionUni( pcCU, pcYuvPred, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred );
      }
      else
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred );
      }
    }
    else
    {
      if ( xCheckIdenticalMotion( pcCU, uiPartAddr ) )
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, REF_PIC_LIST_0, pcYuvPred );
      }
      else
      {
        xPredInterBi  (pcCU, uiPartAddr, iWidth, iHeight, pcYuvPred );
      }
    }
    return;
  }

  for ( iPartIdx = 0; iPartIdx < pcCU->getNumPartitions(); iPartIdx++ )
  {
    pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iWidth, iHeight );

    if ( eRefPicList != REF_PIC_LIST_X )
    {
      if( (sliceType == P_SLICE && pps.getUseWP()) || (sliceType == B_SLICE && pps.getWPBiPred()))
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred, true );
        xWeightedPredictionUni( pcCU, pcYuvPred, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred );
      }
      else
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcYuvPred );
      }
    }
    else
    {
      if ( xCheckIdenticalMotion( pcCU, uiPartAddr ) )
      {
        xPredInterUni (pcCU, uiPartAddr, iWidth, iHeight, REF_PIC_LIST_0, pcYuvPred );
      }
      else
      {
        xPredInterBi  (pcCU, uiPartAddr, iWidth, iHeight, pcYuvPred );
      }
    }
  }
  return;
}

Void TComPrediction::xPredInterUni ( TComDataCU* pcCU, UInt uiPartAddr, Int iWidth, Int iHeight, RefPicList eRefPicList, TComYuv* pcYuvPred, Bool bi )
{
  Int         iRefIdx     = pcCU->getCUMvField( eRefPicList )->getRefIdx( uiPartAddr );           assert (iRefIdx >= 0);
  TComMv      cMv         = pcCU->getCUMvField( eRefPicList )->getMv( uiPartAddr );
  Bool isIntraBC = false;
  if ( pcCU->getSlice()->getRefPic( eRefPicList, iRefIdx )->getPOC() == pcCU->getSlice()->getPOC() )
  {
    isIntraBC = true;
  }
  pcCU->clipMv(cMv);

  for (UInt comp=COMPONENT_Y; comp<pcYuvPred->getNumberValidComponents(); comp++)
  {
    const ComponentID compID=ComponentID(comp);
    xPredInterBlk  (compID,  pcCU, pcCU->getSlice()->getRefPic( eRefPicList, iRefIdx )->getPicYuvRec(), uiPartAddr, &cMv, iWidth, iHeight, pcYuvPred, bi, pcCU->getSlice()->getSPS()->getBitDepth(toChannelType(compID)), isIntraBC);
  }
}

Void TComPrediction::xPredInterBi ( TComDataCU* pcCU, UInt uiPartAddr, Int iWidth, Int iHeight, TComYuv* pcYuvPred )
{
  TComYuv* pcMbYuv;
  Int      iRefIdx[NUM_REF_PIC_LIST_01] = {-1, -1};

  for ( UInt refList = 0; refList < NUM_REF_PIC_LIST_01; refList++ )
  {
    RefPicList eRefPicList = (refList ? REF_PIC_LIST_1 : REF_PIC_LIST_0);
    iRefIdx[refList] = pcCU->getCUMvField( eRefPicList )->getRefIdx( uiPartAddr );

    if ( iRefIdx[refList] < 0 )
    {
      continue;
    }

    assert( iRefIdx[refList] < pcCU->getSlice()->getNumRefIdx(eRefPicList) );

    pcMbYuv = &m_acYuvPred[refList];
    if( pcCU->getCUMvField( REF_PIC_LIST_0 )->getRefIdx( uiPartAddr ) >= 0 && pcCU->getCUMvField( REF_PIC_LIST_1 )->getRefIdx( uiPartAddr ) >= 0 )
    {
      xPredInterUni ( pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcMbYuv, true );
    }
    else
    {
      if ( ( pcCU->getSlice()->getPPS()->getUseWP()       && pcCU->getSlice()->getSliceType() == P_SLICE ) ||
           ( pcCU->getSlice()->getPPS()->getWPBiPred()    && pcCU->getSlice()->getSliceType() == B_SLICE ) )
      {
        xPredInterUni ( pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcMbYuv, true );
      }
      else
      {
        xPredInterUni ( pcCU, uiPartAddr, iWidth, iHeight, eRefPicList, pcMbYuv );
      }
    }
  }

  if ( pcCU->getSlice()->getPPS()->getWPBiPred()    && pcCU->getSlice()->getSliceType() == B_SLICE  )
  {
    xWeightedPredictionBi( pcCU, &m_acYuvPred[REF_PIC_LIST_0], &m_acYuvPred[REF_PIC_LIST_1], iRefIdx[REF_PIC_LIST_0], iRefIdx[REF_PIC_LIST_1], uiPartAddr, iWidth, iHeight, pcYuvPred );
  }
  else if ( pcCU->getSlice()->getPPS()->getUseWP() && pcCU->getSlice()->getSliceType() == P_SLICE )
  {
    xWeightedPredictionUni( pcCU, &m_acYuvPred[REF_PIC_LIST_0], uiPartAddr, iWidth, iHeight, REF_PIC_LIST_0, pcYuvPred );
  }
  else
  {
    xWeightedAverage( &m_acYuvPred[REF_PIC_LIST_0], &m_acYuvPred[REF_PIC_LIST_1], iRefIdx[REF_PIC_LIST_0], iRefIdx[REF_PIC_LIST_1], uiPartAddr, iWidth, iHeight, pcYuvPred, pcCU->getSlice()->getSPS()->getBitDepths() );
  }
}

/**
 * \brief Generate motion-compensated block
 *
 * \param compID     Colour component ID
 * \param cu         Pointer to current CU
 * \param refPic     Pointer to reference picture
 * \param partAddr   Address of block within CU
 * \param mv         Motion vector
 * \param width      Width of block
 * \param height     Height of block
 * \param dstPic     Pointer to destination picture
 * \param bi         Flag indicating whether bipred is used
 * \param  bitDepth  Bit depth
 */


Void TComPrediction::xPredInterBlk(const ComponentID compID, TComDataCU *cu, TComPicYuv *refPic, UInt partAddr, TComMv *mv, Int width, Int height, TComYuv *dstPic, Bool bi, const Int bitDepth, Bool isIntraBC)

{
  Int     refStride  = refPic->getStride(compID);
  Int     dstStride  = dstPic->getStride(compID);
  Int shiftHor=(2+refPic->getComponentScaleX(compID));
  Int shiftVer=(2+refPic->getComponentScaleY(compID));

  Int     refOffset  = (mv->getHor() >> shiftHor) + (mv->getVer() >> shiftVer) * refStride;

  Pel*    ref     = refPic->getAddr(compID, cu->getCtuRsAddr(), cu->getZorderIdxInCtu() + partAddr ) + refOffset;

  Pel*    dst = dstPic->getAddr( compID, partAddr );

  Int     xFrac  = mv->getHor() & ((1<<shiftHor)-1);
  Int     yFrac  = mv->getVer() & ((1<<shiftVer)-1);
  UInt    cxWidth  = width  >> refPic->getComponentScaleX(compID);
  UInt    cxHeight = height >> refPic->getComponentScaleY(compID);

  const ChromaFormat chFmt = cu->getPic()->getChromaFormat();

  if ( yFrac == 0 )
  {
    m_if.filterHor(compID, ref, refStride, dst,  dstStride, cxWidth, cxHeight, xFrac, !bi, chFmt, bitDepth);
  }
  else if ( xFrac == 0 )
  {
    m_if.filterVer(compID, ref, refStride, dst, dstStride, cxWidth, cxHeight, yFrac, true, !bi, chFmt, bitDepth);
  }
  else
  {
    Int   tmpStride = m_filteredBlockTmp[0].getStride(compID);
    Pel*  tmp       = m_filteredBlockTmp[0].getAddr(compID);

    const Int vFilterSize = isLuma(compID) ? NTAPS_LUMA : NTAPS_CHROMA;

    m_if.filterHor(compID, ref - ((vFilterSize>>1) -1)*refStride, refStride, tmp, tmpStride, cxWidth, cxHeight+vFilterSize-1, xFrac, false,      chFmt, bitDepth);
    m_if.filterVer(compID, tmp + ((vFilterSize>>1) -1)*tmpStride, tmpStride, dst, dstStride, cxWidth, cxHeight,               yFrac, false, !bi, chFmt, bitDepth);
  }
}

Void TComPrediction::xWeightedAverage( TComYuv* pcYuvSrc0, TComYuv* pcYuvSrc1, Int iRefIdx0, Int iRefIdx1, UInt uiPartIdx, Int iWidth, Int iHeight, TComYuv* pcYuvDst, const BitDepths &clipBitDepths )
{
  if( iRefIdx0 >= 0 && iRefIdx1 >= 0 )
  {
    pcYuvDst->addAvg( pcYuvSrc0, pcYuvSrc1, uiPartIdx, iWidth, iHeight, clipBitDepths );
  }
  else if ( iRefIdx0 >= 0 && iRefIdx1 <  0 )
  {
    pcYuvSrc0->copyPartToPartYuv( pcYuvDst, uiPartIdx, iWidth, iHeight );
  }
  else if ( iRefIdx0 <  0 && iRefIdx1 >= 0 )
  {
    pcYuvSrc1->copyPartToPartYuv( pcYuvDst, uiPartIdx, iWidth, iHeight );
  }
}

// AMVP
Void TComPrediction::getMvPredAMVP( TComDataCU* pcCU, UInt uiPartIdx, UInt uiPartAddr, RefPicList eRefPicList, TComMv& rcMvPred )
{
  AMVPInfo* pcAMVPInfo = pcCU->getCUMvField(eRefPicList)->getAMVPInfo();

  if( pcAMVPInfo->iN <= 1 )
  {
    rcMvPred = pcAMVPInfo->m_acMvCand[0];

    pcCU->setMVPIdxSubParts( 0, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( pcAMVPInfo->iN, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
    return;
  }

  assert(pcCU->getMVPIdx(eRefPicList,uiPartAddr) >= 0);
  rcMvPred = pcAMVPInfo->m_acMvCand[pcCU->getMVPIdx(eRefPicList,uiPartAddr)];
  return;
}

/** Function for deriving planar intra prediction.
 * \param pSrc        pointer to reconstructed sample array
 * \param srcStride   the stride of the reconstructed sample array
 * \param rpDst       reference to pointer for the prediction sample array
 * \param dstStride   the stride of the prediction sample array
 * \param width       the width of the block
 * \param height      the height of the block
 * \param channelType type of pel array (luma, chroma)
 * \param format      chroma format
 *
 * This function derives the prediction samples for planar mode (intra coding).
 */
//NOTE: Bit-Limit - 24-bit source
Void TComPrediction::xPredIntraPlanar( const Pel* pSrc, Int srcStride, Pel* rpDst, Int dstStride, UInt width, UInt height )
{
  assert(width <= height);

  Int leftColumn[MAX_CU_SIZE+1], topRow[MAX_CU_SIZE+1], bottomRow[MAX_CU_SIZE], rightColumn[MAX_CU_SIZE];
  UInt shift1Dhor = g_aucConvertToBit[ width ] + 2;
  UInt shift1Dver = g_aucConvertToBit[ height ] + 2;

  // Get left and above reference column and row
  for(Int k=0;k<width+1;k++)
  {
    topRow[k] = pSrc[k-srcStride];
  }

  for (Int k=0; k < height+1; k++)
  {
    leftColumn[k] = pSrc[k*srcStride-1];
  }

  // Prepare intermediate variables used in interpolation
  Int bottomLeft = leftColumn[height];
  Int topRight   = topRow[width];

  for(Int k=0;k<width;k++)
  {
    bottomRow[k]  = bottomLeft - topRow[k];
    topRow[k]     <<= shift1Dver;
  }

  for(Int k=0;k<height;k++)
  {
    rightColumn[k]  = topRight - leftColumn[k];
    leftColumn[k]   <<= shift1Dhor;
  }

  const UInt topRowShift = 0;

  // Generate prediction signal
  for (Int y=0;y<height;y++)
  {
    Int horPred = leftColumn[y] + width;
    for (Int x=0;x<width;x++)
    {
      horPred += rightColumn[y];
      topRow[x] += bottomRow[x];

      Int vertPred = ((topRow[x] + topRowShift)>>topRowShift);
      rpDst[y*dstStride+x] = ( horPred + vertPred ) >> (shift1Dhor+1);
    }
  }
}

/** Function for filtering intra DC predictor.
 * \param pSrc pointer to reconstructed sample array
 * \param iSrcStride the stride of the reconstructed sample array
 * \param pDst reference to pointer for the prediction sample array
 * \param iDstStride the stride of the prediction sample array
 * \param iWidth the width of the block
 * \param iHeight the height of the block
 * \param channelType type of pel array (luma, chroma)
 *
 * This function performs filtering left and top edges of the prediction samples for DC mode (intra coding).
 */
Void TComPrediction::xDCPredFiltering( const Pel* pSrc, Int iSrcStride, Pel* pDst, Int iDstStride, Int iWidth, Int iHeight, ChannelType channelType )
{
  Int x, y, iDstStride2, iSrcStride2;

  if (isLuma(channelType) && (iWidth <= MAXIMUM_INTRA_FILTERED_WIDTH) && (iHeight <= MAXIMUM_INTRA_FILTERED_HEIGHT))
  {
    //top-left
    pDst[0] = (Pel)((pSrc[-iSrcStride] + pSrc[-1] + 2 * pDst[0] + 2) >> 2);

    //top row (vertical filter)
    for ( x = 1; x < iWidth; x++ )
    {
      pDst[x] = (Pel)((pSrc[x - iSrcStride] +  3 * pDst[x] + 2) >> 2);
    }

    //left column (horizontal filter)
    for ( y = 1, iDstStride2 = iDstStride, iSrcStride2 = iSrcStride-1; y < iHeight; y++, iDstStride2+=iDstStride, iSrcStride2+=iSrcStride )
    {
      pDst[iDstStride2] = (Pel)((pSrc[iSrcStride2] + 3 * pDst[iDstStride2] + 2) >> 2);
    }
  }

  return;
}

/* Static member function */
Bool TComPrediction::UseDPCMForFirstPassIntraEstimation(TComTU &rTu, const UInt uiDirMode)
{
  return (rTu.getCU()->isRDPCMEnabled(rTu.GetAbsPartIdxTU()) ) &&
          rTu.getCU()->getCUTransquantBypass(rTu.GetAbsPartIdxTU()) &&
          (uiDirMode==HOR_IDX || uiDirMode==VER_IDX);
}

Void TComPrediction::preCalcPaletteIndexRD(TComDataCU* pcCU, Pel *Palette[3], Pel* pSrc[3], UInt uiWidth, UInt uiHeight, UInt paletteSize, TComRdCost *pcCost, UInt calcErroBits)
{
  Bool bLossless = pcCU->getCUTransquantBypass(0);
  Int iErrorLimit = bLossless ? 0 : 3 * getPaletteErrLimit()*getPaletteErrLimit();

  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb); 

  UInt paletteIdx, uiMinError, uiBestIdx, uiPos;
  UChar useEscapeFlag=0;
  Int maxSpsPaletteSize = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();

  Pel distAdjY = DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA] - 8) << 1);
  Pel distAdjC = DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA] - 8) << 1);

  Short iTemp;
  UInt uiAbsError;

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      UInt* indError = m_indError[uiPos];
      Int localAdjC = distAdjC;
      Bool discardChroma = uiY&uiScaleY || uiX&uiScaleX;
      if (discardChroma) localAdjC+=SCM_V0034_PALETTE_CHROMA_SHIFT_ADJ;
      UInt round = localAdjC ? (1 << (localAdjC-1)) : 0;
      uiBestIdx=0;
      uiMinError = MAX_UINT;
     

      paletteIdx = 0;
      while (paletteIdx < paletteSize)
      {
        if( bLossless )
        {
          if( Palette[0][paletteIdx] != pSrc[0][uiPos] )
          {
            indError[paletteIdx] = uiAbsError = MAX_UINT;
          }
          else
          {
            uiAbsError  = abs(Palette[1][paletteIdx] - pSrc[1][uiPosC]);
            uiAbsError += abs(Palette[2][paletteIdx] - pSrc[2][uiPosC]);
            if (uiAbsError && !discardChroma)
            {
              indError[paletteIdx] = uiAbsError = MAX_UINT;
            }
            else
            {
              uiAbsError = std::min(1U, uiAbsError);
              indError[paletteIdx] = 0;
            }
          }
        }
        else
        {
          iTemp = Palette[1][paletteIdx] - pSrc[1][uiPosC];
          uiAbsError = iTemp * iTemp;
          iTemp = Palette[2][paletteIdx] - pSrc[2][uiPosC];
          uiAbsError += iTemp * iTemp;
          uiAbsError = (uiAbsError + round) >> localAdjC;
          iTemp = Palette[0][paletteIdx] - pSrc[0][uiPos];
          uiAbsError += (iTemp * iTemp) >> distAdjY;
          indError[paletteIdx] = discardChroma ? (iTemp * iTemp) >> distAdjY : uiAbsError;
        }

        if (uiAbsError < uiMinError)
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
        }

        paletteIdx++;
      }
      m_cIndexBlock[uiPos] = uiBestIdx;

      UInt errorTemp;

      if( discardChroma && bLossless && uiMinError != MAX_UINT )
      {
        uiMinError = 0;
      }

      if (uiMinError > iErrorLimit || calcErroBits)
      {
        Double rdCost = MAX_DOUBLE;
        if (pcCU->getCUTransquantBypass(0))
        {
          errorTemp = 0;
          UInt uiNumTotalBits = pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA);
          if (!discardChroma) uiNumTotalBits += pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_CHROMA)<<1;

          rdCost += pcCost->getLambda() * uiNumTotalBits;
          if (uiMinError > iErrorLimit)
          {
            m_cIndexBlock[uiPos] -= maxSpsPaletteSize;
            useEscapeFlag = 1;
          }
        }
        else
        {
          Pel pOrg[3] = { pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC] };
          rdCost = calcPixelPredRD(pcCU, pOrg, pcCost, &errorTemp, discardChroma);

          if (rdCost < uiMinError && uiMinError > iErrorLimit)
          {
            m_cIndexBlock[uiPos] -= maxSpsPaletteSize;
            useEscapeFlag = 1;
          }
        }
        indError[MAX_PALETTE_SIZE - 1] = (UInt)rdCost;
        indError[MAX_PALETTE_SIZE] = (UInt)errorTemp;
      } 
      m_cPosBlock[uiPos] = uiPos; 
    }
  }

  pcCU->setPaletteEscapeSubParts(0, useEscapeFlag,0, pcCU->getDepth(0));
  pcCU->setPaletteEscapeSubParts(1, useEscapeFlag,0, pcCU->getDepth(0));
  pcCU->setPaletteEscapeSubParts(2, useEscapeFlag,0, pcCU->getDepth(0));

}

Void TComPrediction::preCalcPaletteIndex(TComDataCU* pcCU, Pel *Palette[3], Pel* pSrc[3], UInt uiWidth, UInt uiHeight, UInt paletteSize)
{
  Bool bLossless = pcCU->getCUTransquantBypass(0);
  Int iErrorLimit = bLossless ? 0 : 3 * getPaletteErrLimit();
  UInt uiPos;
  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);

  UInt uiBestIdx = 0;
  UChar useEscapeFlag=0;

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      UInt paletteIdx = 0;
      UInt uiMinError = MAX_UINT;
      while (paletteIdx < paletteSize)
      {
        UInt uiAbsError = MAX_UINT;
        if ( bLossless )
        {
          uiAbsError = abs( Palette[0][paletteIdx] - pSrc[0][uiPos] ) + abs( Palette[1][paletteIdx] - pSrc[1][uiPosC] ) + abs( Palette[2][paletteIdx] - pSrc[2][uiPosC] );
        }
        else
        {
          uiAbsError = ( abs(Palette[0][paletteIdx] - pSrc[0][uiPos])  >> DISTORTION_PRECISION_ADJUSTMENT(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]  -8) )
                     + ( abs(Palette[1][paletteIdx] - pSrc[1][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) )
                     + ( abs(Palette[2][paletteIdx] - pSrc[2][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) );
        }
        if (uiAbsError < uiMinError)
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }
      m_cIndexBlock[uiPos] = uiBestIdx;
      if (uiMinError > iErrorLimit)
      {
        m_cIndexBlock[uiPos] -= pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();
        useEscapeFlag=1;
      }
    }
  }

  pcCU->setPaletteEscapeSubParts(0, useEscapeFlag,0, pcCU->getDepth(0));
  pcCU->setPaletteEscapeSubParts(1, useEscapeFlag,0, pcCU->getDepth(0));
  pcCU->setPaletteEscapeSubParts(2, useEscapeFlag,0, pcCU->getDepth(0));
}

Void  TComPrediction::reorderPalette(TComDataCU* pcCU, Pel *pPalette[3], UInt uiNumComp)
{
  UInt uiPaletteSizePrev, uiDictMaxSize;
  Pel * pPalettePrev[3];
  UInt maxPaletteSize = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();
  UInt maxPalettePredSize = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxPredSize();
  Pel* pPaletteTemp[3];
  for (UInt ch = 0; ch < 3; ch++)
  {
    pPaletteTemp[ch] = (Pel*)xMalloc(Pel, maxPaletteSize);
  }
  ComponentID compBegin = COMPONENT_Y;

  for (UInt comp = compBegin; comp < compBegin + uiNumComp; comp++)
  {
    pPalettePrev[comp] = pcCU->getPalettePred(pcCU, pcCU->getZorderIdxInCtu(), comp, uiPaletteSizePrev);
    for (UInt i = 0; i < maxPaletteSize; i++)
    {
      pPaletteTemp[comp][i] = pPalette[comp][i];
    }
  }

  uiDictMaxSize = pcCU->getPaletteSize(compBegin, 0);

  UInt uiIdxPrev = 0, uiIdxCurr = 0;
  Bool bReused = false;
  Bool *bPredicted, *bReusedPrev;
  bPredicted  = (Bool*)xMalloc(Bool, maxPaletteSize + 1);
  bReusedPrev = (Bool*)xMalloc(Bool, maxPalettePredSize + 1);
  memset(bPredicted, 0, sizeof(Bool)*(maxPaletteSize + 1));
  memset(bReusedPrev, 0, sizeof(Bool)*(maxPalettePredSize + 1));

  Int numPaletteRceived = uiDictMaxSize;
  UInt numPalettePredicted = 0;

  for (uiIdxCurr = 0; uiIdxCurr < uiDictMaxSize; uiIdxCurr++)
  {
    bReused = false;
    Int iCounter = 0;

    for (uiIdxPrev = 0; uiIdxPrev < uiPaletteSizePrev; uiIdxPrev++)
    {
      iCounter = 0;

      for (UInt comp = compBegin; comp < compBegin + uiNumComp; comp++)
      {
        if (pPalettePrev[comp][uiIdxPrev] == pPalette[comp][uiIdxCurr])
        {
          iCounter++;
        }
      }
      if (iCounter == uiNumComp)
      {
        bReused = true;
        break;
      }
    }
    bReusedPrev[uiIdxPrev] = bReused;
    bPredicted[uiIdxCurr] = bReused;
    if (bPredicted[uiIdxCurr])
    {
      numPaletteRceived--;
      numPalettePredicted++;
    }
  }

  assert( numPaletteRceived >= 0 );
  assert( numPalettePredicted <= uiDictMaxSize );

  for (uiIdxPrev = 0; uiIdxPrev < maxPalettePredSize; uiIdxPrev++)
  {
    for (UInt comp = compBegin; comp < compBegin + uiNumComp; comp++)
    {
      pcCU->setPrevPaletteReusedFlagSubParts(comp, bReusedPrev[uiIdxPrev], uiIdxPrev, 0, pcCU->getDepth(0));
    }
  }
  uiIdxCurr = 0;
  for (UInt uiPrevIdx = 0; uiPrevIdx < uiPaletteSizePrev; uiPrevIdx++)
  {
    if (bReusedPrev[uiPrevIdx])
    {
      for (UInt comp = compBegin; comp < compBegin + uiNumComp; comp++)
      {
        pPalette[comp][uiIdxCurr] = pPalettePrev[comp][uiPrevIdx];
      }
      uiIdxCurr++;
    }
  }

  for (UInt uiIdx = 0; uiIdx < uiDictMaxSize; uiIdx++)
  {
    if (bPredicted[uiIdx] == 0)
    {
      for (UInt comp = compBegin; comp < compBegin + uiNumComp; comp++)
      {
        pPalette[comp][uiIdxCurr] = pPaletteTemp[comp][uiIdx];
      }
      uiIdxCurr++;
    }
  }
  for (UInt ch = 0; ch < 3; ch++)
  {
    if (pPaletteTemp[ch])
    {
      xFree(pPaletteTemp[ch]);
      pPaletteTemp[ch] = NULL;
    }
  }
  if (bPredicted)
  {
    xFree(bPredicted);
    bPredicted = NULL;
  }
  if (bReusedPrev)
  {
    xFree(bReusedPrev);
    bReusedPrev = NULL;
  }
}

UInt  TComPrediction::findCandidatePalettePredictors(UInt paletteIndBest[], TComDataCU* pcCU, Pel *Palette[3], Pel* pPred[3], UInt paletteSizeTemp, UInt maxNoPredInd)
{
  UInt uiAbsError=0, uiMinError;
  UInt palettePredError[MAX_PALETTE_PRED_SIZE];
  BitDepths bitDepths = pcCU->getSlice()->getSPS()->getBitDepths();


  for(int t = 0; t < pcCU->getLastPaletteInLcuSizeFinal(0); t++)
  {
    uiAbsError=0;
    Int iTemp=pPred[0][t] - Palette[0][paletteSizeTemp]; 
    uiAbsError += (iTemp * iTemp) >> DISTORTION_PRECISION_ADJUSTMENT((bitDepths.recon[CHANNEL_TYPE_LUMA] - 8) << 1);
    iTemp=pPred[1][t] - Palette[1][paletteSizeTemp];     
    uiAbsError += (iTemp * iTemp) >> DISTORTION_PRECISION_ADJUSTMENT((bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8) << 1);
    iTemp=pPred[2][t] - Palette[2][paletteSizeTemp];     
    uiAbsError += (iTemp * iTemp) >> DISTORTION_PRECISION_ADJUSTMENT((bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8) << 1);

    palettePredError[t] = uiAbsError;
    paletteIndBest[t] = t;
  }

  UInt bestInd;
  for(int t=0; t < maxNoPredInd; t++)
  {
    bestInd = t;
    uiMinError = palettePredError[t];

    for (UInt l=t+1; l < pcCU->getLastPaletteInLcuSizeFinal(0); l++)
    {
      if (palettePredError[l] < uiMinError)
      {
        bestInd=l;
        uiMinError=palettePredError[l];
      }
    }

    swap(palettePredError[bestInd], palettePredError[t]);
    swap(paletteIndBest[bestInd], paletteIndBest[t]);
  }

  UInt maxPredCheck=min((UInt)pcCU->getLastPaletteInLcuSizeFinal(0), maxNoPredInd);

  return(maxPredCheck);
}

Void  TComPrediction::derivePaletteLossy( TComDataCU* pcCU, Pel *Palette[3], Pel* pSrc[3],  UInt uiWidth, UInt uiHeight, UInt uiStride, UInt &paletteSize, TComRdCost *pcCost )
{
  Int iErrorLimit = getPaletteErrLimit();
  UInt uiTotalSize = uiHeight*uiWidth;
  SortingElement *psList = new SortingElement [uiTotalSize];
  SortingElement sElement;
  UInt uiDictMaxSize = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();
  SortingElement *pListSort = new SortingElement [uiDictMaxSize + 1];
  UInt uiIdx = 0;
  UInt uiPos;
  Int last = -1;
  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);

  SortingElement *psListHistogram = new SortingElement[uiTotalSize];
  SortingElement *psInitial = new SortingElement[uiTotalSize];
  UInt uiHisIdx = 0;

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      sElement.setAll(pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC]);
      Int i = 0;
      for (i = uiHisIdx - 1; i >= 0; i--)
      {
        if (psListHistogram[i].EqualData(sElement))
        {
          psListHistogram[i].addElement(sElement);
          break;
        }
      }
      if (i == -1)
      {
        psListHistogram[uiHisIdx].copyDataFrom(sElement);
        psListHistogram[uiHisIdx].uiCnt = 1;
        uiHisIdx++;
      }
    }
  }

  UInt uiHisCnt, uiMaxIdx;
  UInt uiLimit = ((uiHeight << 2)*iErrorLimit) >> 7;
  uiLimit = (uiLimit > (uiHeight >> 1)) ? uiLimit : (uiHeight >> 1);

  Bool bOtherPeakExist;
  while (true)
  {
    uiHisCnt = psListHistogram[0].uiCnt;
    uiMaxIdx = 0;
    for (UInt j = 1; j < uiHisIdx; j++)
    {
      if (psListHistogram[j].uiCnt >= uiHisCnt)
      {
        uiHisCnt = psListHistogram[j].uiCnt;
        uiMaxIdx = j;
      }
    }

    if (uiHisCnt >= uiLimit)
    {
      bOtherPeakExist = false;
      for (UInt j = 0; j < uiHisIdx; j++)
      {
        if (psListHistogram[j].uiCnt >= (uiHisCnt >> 1) && j != uiMaxIdx)
        {
          if (psListHistogram[uiMaxIdx].almostEqualData(psListHistogram[j], iErrorLimit >> 2, pcCU->getSlice()->getSPS()->getBitDepths()))
          {
            bOtherPeakExist = true;
          }
        }
      }

      if (!bOtherPeakExist)
      {
        psList[uiIdx].copyAllFrom(psListHistogram[uiMaxIdx]);
        psInitial[uiIdx].copyAllFrom(psListHistogram[uiMaxIdx]);
        last = uiIdx;
        uiIdx++;

        for (UInt j = 0; j < uiHisIdx; j++)
        {
          if (psListHistogram[uiMaxIdx].almostEqualData(psListHistogram[j], iErrorLimit >> 2, pcCU->getSlice()->getSPS()->getBitDepths()) && j != uiMaxIdx)
          {
            psListHistogram[j].ResetElement();
          }
        }
      }

      psListHistogram[uiMaxIdx].ResetElement();
    }
    else
    {
      break;
    }
  }

  UInt uiInitialIdx = uiIdx;
  Bool bMatched;

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      sElement.setAll(pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC]);
      bMatched = false;
      for (Int i = 0; i < uiInitialIdx; i++)
      {
        bMatched |= psInitial[i].EqualData(sElement);
      }

      if (!bMatched)
      {
        Int besti = last, bestSAD = (last == -1) ? MAX_UINT : psList[last].getSAD(sElement, pcCU->getSlice()->getSPS()->getBitDepths());
        if (bestSAD)
        {
          for (Int i = uiIdx - 1; i >= 0; i--)
          {
            UInt sad = psList[i].getSAD(sElement, pcCU->getSlice()->getSPS()->getBitDepths());
            if (sad < bestSAD)
            {
              bestSAD = sad;
              besti = i;
              if (!sad) break;
            }
          }
        }

        if (besti >= 0 && psList[besti].almostEqualData(sElement, iErrorLimit, pcCU->getSlice()->getSPS()->getBitDepths()))
        {
          psList[besti].addElement(sElement);
          last = besti;
        }
        else
        {
          psList[uiIdx].copyDataFrom(sElement);
          psList[uiIdx].uiCnt = 1;
          last = uiIdx;
          uiIdx++;
        }
      }
    }
  }

  for (Int i = 0; i < uiDictMaxSize; i++)
  {
    pListSort[i].uiCnt  = 0;
    pListSort[i].setAll(0, 0, 0) ;
  }

  //bubble sorting
  uiDictMaxSize = 1;
  for (Int i = 0; i < uiIdx; i++)
  {
    if( psList[i].uiCnt > pListSort[uiDictMaxSize-1].uiCnt )
    {
      Int j;
      for (j = uiDictMaxSize; j > 0; j--)
      {
        if (psList[i].uiCnt > pListSort[j-1].uiCnt)
        {
          pListSort[j].copyAllFrom (pListSort[j-1]);
          uiDictMaxSize = std::min(uiDictMaxSize + 1, pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize());
        }
        else
        {
          break;
        }
      }
      pListSort[j].copyAllFrom (psList[i]) ;
    }
  }

  Int paletteIndPred[MAX_PALETTE_SIZE];
  memset(paletteIndPred, 0, MAX_PALETTE_SIZE*sizeof(Int));

  paletteSize = 0;
  Pel *pPred[3]  = { pcCU->getLastPaletteInLcuFinal(0), pcCU->getLastPaletteInLcuFinal(1), pcCU->getLastPaletteInLcuFinal(2) };
  UInt uiNumTotalBits = pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) + (pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_CHROMA)<<1);
  Double bitCost = pcCost->getLambda() * uiNumTotalBits;
  BitDepths bitDepths = pcCU->getSlice()->getSPS()->getBitDepths();
  for (Int i = 0; i < pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize(); i++)
  {
    if( pListSort[i].uiCnt )
    {
      Int iHalf = pListSort[i].uiCnt>>1;
      Palette[0][paletteSize] = (pListSort[i].uiSumData[0]+iHalf)/pListSort[i].uiCnt;
      Palette[1][paletteSize] = (pListSort[i].uiSumData[1]+iHalf)/pListSort[i].uiCnt;
      Palette[2][paletteSize] = (pListSort[i].uiSumData[2]+iHalf)/pListSort[i].uiCnt;

      Int best = -1;
      if( iErrorLimit )
      {
        Double pal[3] = { pListSort[i].uiSumData[0]/(Double)pListSort[i].uiCnt,
                          pListSort[i].uiSumData[1]/(Double)pListSort[i].uiCnt,
                          pListSort[i].uiSumData[2]/(Double)pListSort[i].uiCnt };

        Double err      = pal[0] - Palette[0][paletteSize];
        Double bestCost = (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA]-8)) );
        err = pal[1] - Palette[1][paletteSize]; bestCost += (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA]-8)) );
        err = pal[2] - Palette[2][paletteSize]; bestCost += (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA]-8)) );
        bestCost = bestCost * pListSort[i].uiCnt + bitCost;

        for(int t=0; t<pcCU->getLastPaletteInLcuSizeFinal(0); t++)
        {
          err = pal[0] - pPred[0][t];
          Double cost = (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA]-8)) );
          err = pal[1] - pPred[1][t]; cost += (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA]-8)) );
          err = pal[2] - pPred[2][t]; cost += (err*err) / ( 1<<(2*DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA]-8)) );
          cost *= pListSort[i].uiCnt;
          if(cost < bestCost)
          {
            best = t;
            bestCost = cost;
          }
        }
        if( best != -1 )
        {
          Palette[0][paletteSize] = pPred[0][best];
          Palette[1][paletteSize] = pPred[1][best];
          Palette[2][paletteSize] = pPred[2][best];
        }
        paletteIndPred[paletteSize]=best;
      }

      Bool bDuplicate = false;
      if( pListSort[i].uiCnt == 1 && best == -1 )
      {
        bDuplicate = true;
      }
      else
      {
        for( Int t=0; t<paletteSize; t++)
        {
          if( Palette[0][paletteSize] == Palette[0][t] && Palette[1][paletteSize] == Palette[1][t] && Palette[2][paletteSize] == Palette[2][t] )
          {
            bDuplicate = true;
            break;
          }
        }
      }
      if( !bDuplicate ) paletteSize++;
    }
    else
    {
      break;
    }
  }

  UInt palettePredSamples[MAX_PALETTE_SIZE][5];
  memset(palettePredSamples, 0, 5*MAX_PALETTE_SIZE*sizeof(UInt));
  Int iErrorLimitSqr = 3 * getPaletteErrLimit()*getPaletteErrLimit();

  UInt uiAbsError;
  UInt uiMinError;

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      UInt uiBestIdx=0, paletteIdx = 0;
      Bool discardChroma = uiY&uiScaleY || uiX&uiScaleX;
      uiMinError = MAX_UINT;

      while (paletteIdx < paletteSize)
      {
        Int iTemp=Palette[0][paletteIdx] - pSrc[0][uiPos];
        uiAbsError = (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
        iTemp=Palette[1][paletteIdx] - pSrc[1][uiPosC];
        uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
        iTemp=Palette[2][paletteIdx] - pSrc[2][uiPosC];
        uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));

        if (uiAbsError < uiMinError)
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }

      UInt escape=0;
      if (uiMinError > iErrorLimitSqr)
      {
        Pel pOrg[3]={ pSrc[0][uiPos],  pSrc[1][uiPosC],  pSrc[2][uiPosC]};
        UInt errorTemp;
        Double rdCost=calcPixelPredRD(pcCU, pOrg, pcCost, &errorTemp); 
        if (rdCost<uiMinError) 
        {
          escape=1;
        }
      }

      if (escape==0)
      {
        palettePredSamples[uiBestIdx][0]++;
        palettePredSamples[uiBestIdx][1] += pSrc[0][uiPos];
        if (!discardChroma)
        {
          palettePredSamples[uiBestIdx][2] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4] += SCM_V0034_PALETTE_CHROMA_SETTINGS;
        }
        else
        {
          palettePredSamples[uiBestIdx][2] += pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4]++;
        }
        m_cIndexBlock[uiPos] = uiBestIdx;
      }
      else
      {
        m_cIndexBlock[uiPos]=-1;
      }
    }
  }

  UInt paletteIndBest[MAX_PALETTE_PRED_SIZE];

  UInt   paletteSizeTemp=0;
  for (Int i = 0; i < paletteSize; i++)
  {
    if(palettePredSamples[i][0] > 0)
    {
      Palette[0][paletteSizeTemp] = (palettePredSamples[i][1]+palettePredSamples[i][0]/2)/palettePredSamples[i][0];
      Palette[1][paletteSizeTemp] = (palettePredSamples[i][2]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];
      Palette[2][paletteSizeTemp] = (palettePredSamples[i][3]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];

      Double dMinError = pcCost->getLambda()*(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]+2*pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]); 


      for (UInt uiY = 0; uiY < uiHeight; uiY++)
      {
        for (UInt uiX = 0; uiX < uiWidth; uiX++)
        {
          uiPos = uiY * uiWidth + uiX;
          if (m_cIndexBlock[uiPos]==i)
          {
            UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

            Int iTemp=Palette[0][paletteSizeTemp] - pSrc[0][uiPos];
            dMinError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
            iTemp=Palette[1][paletteSizeTemp] - pSrc[1][uiPosC];
            dMinError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
            iTemp=Palette[2][paletteSizeTemp] - pSrc[2][uiPosC];
            dMinError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
          }
        }
      }

      UInt maxPredCheck=findCandidatePalettePredictors(paletteIndBest, pcCU, Palette, pPred, paletteSizeTemp, MAX_PRED_CHEK);
  
      Int best=-1;
      if (paletteIndPred[i]>=0)
      {
        for (int t=0; t<maxPredCheck; t++)
        {
          if (paletteIndPred[i]==paletteIndBest[t])
          {
            best=1;
          }
        }
        if (best==-1)
        {
          paletteIndBest[maxPredCheck]=paletteIndPred[i];
          maxPredCheck++;
        }
      }

      best=-1;
      UInt testedPalettePred;

      for(int t=0; t<maxPredCheck; t++)
      {
        testedPalettePred=paletteIndBest[t];

        uiAbsError=0;
        for (UInt uiY = 0; uiY < uiHeight; uiY++)
        {
          for (UInt uiX = 0; uiX < uiWidth; uiX++)
          {
            uiPos = uiY * uiWidth + uiX;
            if (m_cIndexBlock[uiPos]==i)
            {

              UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
              Int iTemp=pPred[0][testedPalettePred] - pSrc[0][uiPos];
              uiAbsError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
              iTemp=pPred[1][testedPalettePred] - pSrc[1][uiPosC];
              uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
              iTemp=pPred[2][testedPalettePred] - pSrc[2][uiPosC];
              uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
            }
          }
          if (uiAbsError>dMinError)
          {
            break;
          }
        }

        if (uiAbsError < dMinError || (uiAbsError == dMinError && best>testedPalettePred))
        {
          best = testedPalettePred;
          dMinError = uiAbsError;
        }
      }


      if( best != -1 )
      {
        Palette[0][paletteSizeTemp] = pPred[0][best];
        Palette[1][paletteSizeTemp] = pPred[1][best];
        Palette[2][paletteSizeTemp] = pPred[2][best];
      }


      Bool bDuplicate = false;
      if( palettePredSamples[i][0] == 1 && best == -1 )
      {
        bDuplicate = true;
      }
      else
      {
        for( Int t=0; t<paletteSizeTemp; t++)
        {
          if( Palette[0][paletteSizeTemp] == Palette[0][t] && Palette[1][paletteSizeTemp] == Palette[1][t] && Palette[2][paletteSizeTemp] == Palette[2][t] )
          {
            bDuplicate = true;
            break;
          }
        }
      }
      if( !bDuplicate ) paletteSizeTemp++;
    }
  }

  paletteSize=paletteSizeTemp;

  delete[] psList;
  delete[] pListSort;

  delete[] psListHistogram;
  delete[] psInitial;
}

Void  TComPrediction::derivePaletteLossyIterative(TComDataCU* pcCU, Pel *Palette[3], Pel* pSrc[3],  UInt uiWidth, UInt uiHeight, UInt uiStride, UInt &paletteSize, TComRdCost *pcCost)
{
  UInt uiPos, paletteIdx = 0, uiBestIdx;

  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);

  Int distAdjY = DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA] - 8) << 1);
  Int distAdjC = DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA] - 8) << 1);
  UInt palettePredSamples[MAX_PALETTE_SIZE][5], noSamples[MAX_PALETTE_SIZE];
  memset(palettePredSamples, 0, 5*MAX_PALETTE_SIZE*sizeof(UInt));
  Int iErrorLimitSqr = pcCU->getCUTransquantBypass(0) ? 0 : 3 * getPaletteErrLimit()*getPaletteErrLimit(); //ZF

  Pel *pPred[3]  = { pcCU->getLastPaletteInLcuFinal(0), pcCU->getLastPaletteInLcuFinal(1), pcCU->getLastPaletteInLcuFinal(2) };
  Pel pPaletteTemp[3][MAX_PALETTE_SIZE];

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;

      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      Int  localAdjC = distAdjC;
      Bool discardChroma = uiY&uiScaleY || uiX&uiScaleX;
      if (discardChroma) localAdjC+=SCM_V0034_PALETTE_CHROMA_SHIFT_ADJ;

      uiBestIdx=0;
      UInt uiMinError = MAX_UINT;

      paletteIdx=0;
      while (paletteIdx < paletteSize)
      {
        Int iTemp=Palette[0][paletteIdx] - pSrc[0][uiPos];
        UInt uiAbsError = ( iTemp * iTemp ) >> distAdjY;
        iTemp           = Palette[1][paletteIdx] - pSrc[1][uiPosC];
        uiAbsError     += ( iTemp * iTemp ) >> localAdjC;
        iTemp           = Palette[2][paletteIdx] - pSrc[2][uiPosC];
        uiAbsError     += ( iTemp * iTemp ) >> localAdjC;

        if (uiAbsError < uiMinError)
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }

      UInt escape=0;
      if (uiMinError > iErrorLimitSqr)
      {
        UInt errorTemp;
        Pel pOrg[3]={ pSrc[0][uiPos],  pSrc[1][uiPosC],  pSrc[2][uiPosC]};
        Double rdCost=calcPixelPredRD(pcCU, pOrg, pcCost, &errorTemp, discardChroma);

        if (rdCost<uiMinError) 
        {
          escape=1;
        }
      }

      if (escape==0)
      {
        palettePredSamples[uiBestIdx][0]++;
        palettePredSamples[uiBestIdx][1] += pSrc[0][uiPos];
        if (!discardChroma)
        {
          palettePredSamples[uiBestIdx][2] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4] += SCM_V0034_PALETTE_CHROMA_SETTINGS;
        }
        else
        {
          palettePredSamples[uiBestIdx][2] += pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4]++;
        }
        m_cIndexBlock[uiPos] = uiBestIdx;
      }
      else
      {
        m_cIndexBlock[uiPos]=-1;
      }
    }
  }

  UInt paletteIndBest[MAX_PALETTE_PRED_SIZE];
  UInt   paletteSizeTemp=0;
  for (Int i = 0; i < paletteSize; i++)
  {
    if(palettePredSamples[i][0]>0)
    {
      pPaletteTemp[0][paletteSizeTemp] = (palettePredSamples[i][1]+palettePredSamples[i][0]/2)/palettePredSamples[i][0];
      pPaletteTemp[1][paletteSizeTemp] = (palettePredSamples[i][2]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];
      pPaletteTemp[2][paletteSizeTemp] = (palettePredSamples[i][3]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];

      noSamples[paletteSizeTemp]=palettePredSamples[i][0];

      Double uiMinError = pcCost->getLambda()*(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]+2*pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]), 
        uiAbsError;

      for (UInt uiY = 0; uiY < uiHeight; uiY++)
      {
        for (UInt uiX = 0; uiX < uiWidth; uiX++)
        {
          uiPos = uiY * uiWidth + uiX;
          if (m_cIndexBlock[uiPos]==i)
          {

            UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
            Int iTemp=pPaletteTemp[0][paletteSizeTemp] - pSrc[0][uiPos];
            Int  localAdjC = distAdjC;
            if (uiY&uiScaleY || uiX&uiScaleX) localAdjC+=SCM_V0034_PALETTE_CHROMA_SHIFT_ADJ;
            uiMinError += ( iTemp * iTemp ) >> distAdjY;
            iTemp       = pPaletteTemp[1][paletteSizeTemp] - pSrc[1][uiPosC];
            uiMinError += ( iTemp * iTemp ) >> localAdjC;
            iTemp       = pPaletteTemp[2][paletteSizeTemp] - pSrc[2][uiPosC];
            uiMinError += ( iTemp * iTemp ) >> localAdjC;
          }
        }
      }


      UInt maxPredCheck=findCandidatePalettePredictors(paletteIndBest, pcCU, Palette, pPred, paletteSizeTemp, MAX_PRED_CHEK);
      Int best=-1;
      UInt testedPalettePred;

      for(int t=0; t<maxPredCheck; t++)
      {
        testedPalettePred=paletteIndBest[t];

        uiAbsError=0;
        for (UInt uiY = 0; uiY < uiHeight; uiY++)
        {
          for (UInt uiX = 0; uiX < uiWidth; uiX++)
          {
            uiPos = uiY * uiWidth + uiX;
            if (m_cIndexBlock[uiPos]==i)
            {

              UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

              Int iTemp=pPred[0][testedPalettePred] - pSrc[0][uiPos];
              Int  localAdjC = distAdjC;
              if (uiY&uiScaleY || uiX&uiScaleX) localAdjC+=SCM_V0034_PALETTE_CHROMA_SHIFT_ADJ;
              uiAbsError += ( iTemp * iTemp ) >> distAdjY;
              iTemp       = pPred[1][testedPalettePred] - pSrc[1][uiPosC];
              uiAbsError += ( iTemp * iTemp ) >> localAdjC;
              iTemp       = pPred[2][testedPalettePred] - pSrc[2][uiPosC];
              uiAbsError += ( iTemp * iTemp ) >> localAdjC;
            }
          }
          if (uiAbsError>uiMinError)
          {
            break;
          }
        }

        if (uiAbsError < uiMinError || (uiAbsError == uiMinError && best>testedPalettePred))
        {
          best = testedPalettePred;
          uiMinError = uiAbsError;
        }
      }

      if( best != -1 )
      {
        pPaletteTemp[0][paletteSizeTemp] = pPred[0][best];
        pPaletteTemp[1][paletteSizeTemp] = pPred[1][best];
        pPaletteTemp[2][paletteSizeTemp] = pPred[2][best];
      }


      Bool bDuplicate = false;
      if( palettePredSamples[i][0] == 1 && best == -1 )
      {
        bDuplicate = true;
      }
      else
      {
        for( Int t=0; t<paletteSizeTemp; t++)
        {
          if( pPaletteTemp[0][paletteSizeTemp] == pPaletteTemp[0][t] && pPaletteTemp[1][paletteSizeTemp] == pPaletteTemp[1][t] && pPaletteTemp[2][paletteSizeTemp] == pPaletteTemp[2][t] )
          {
            bDuplicate = true;
            break;
          }
        }
      }
      if( !bDuplicate ) paletteSizeTemp++;
    }
  }

  paletteSize=paletteSizeTemp;

  for (paletteIdx=0; paletteIdx<paletteSize; paletteIdx++)
  {
    uiBestIdx=paletteIdx;
    UInt maxSamples=noSamples[paletteIdx];

    for (UInt paletteIdxSec=paletteIdx+1; paletteIdxSec<paletteSize; paletteIdxSec++)
    {
      if (noSamples[paletteIdxSec]>maxSamples)
      {
        uiBestIdx=paletteIdxSec;
        maxSamples=noSamples[paletteIdxSec];
      }
    }
    
    Palette[0][paletteIdx]=pPaletteTemp[0][uiBestIdx];
    Palette[1][paletteIdx]=pPaletteTemp[1][uiBestIdx];
    Palette[2][paletteIdx]=pPaletteTemp[2][uiBestIdx];
    
    pPaletteTemp[0][uiBestIdx]=pPaletteTemp[0][paletteIdx];
    pPaletteTemp[1][uiBestIdx]=pPaletteTemp[1][paletteIdx];
    pPaletteTemp[2][uiBestIdx]=pPaletteTemp[2][paletteIdx];

    noSamples[uiBestIdx]=noSamples[paletteIdx];
    
  }
}

Void TComPrediction::derivePaletteLossless(TComDataCU* pcCU, Pel *Palette[3], Pel* pSrc[3], UInt uiWidth, UInt uiHeight, UInt uiStride, UInt &paletteSize, Bool forcePalettePrediction)
{
  std::vector<SortingElement> psList;
  SortingElement sElement;
  Int uiIdx = 0;
  UInt uiPos;

  const UInt maxPaletteSizeSPS = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();
  paletteSize = 0;

  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);

  uiIdx = 0;
  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      Bool discardChroma = uiY&uiScaleY || uiX&uiScaleX;
      Int  defIdx = -1, defSAD = MAX_INT;
      Int  discIdx = -1, discSAD = MAX_INT;

      Int i = 0;
      sElement.setAll(pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC]);
      for (i = uiIdx - 1; i >= 0; i--)
      {
        if( psList[i].uiData[0] == sElement.uiData[0] && psList[i].uiData[1] == sElement.uiData[1] && psList[i].uiData[2] == sElement.uiData[2] )
        {
          psList[i].uiCnt++;
          if ( !discardChroma )
          {
            psList[i].uiLastCnt = 1;
          }
          break;
        }

        if( (uiScaleX||uiScaleY) && psList[i].uiData[0] == sElement.uiData[0] )
        {
          Int sad = abs(psList[i].uiData[1] - pSrc[1][uiPosC]) + abs(psList[i].uiData[2] - pSrc[2][uiPosC]);
          if( !discardChroma && !psList[i].uiLastCnt && sad < discSAD )
          {
            discIdx = i;
            discSAD = sad;
          }
          if( discardChroma && sad < defSAD )
          {
            defIdx = i;
            defSAD = sad;
          }
        }
      }
      if( discIdx >= 0 )
      {
        psList[discIdx].uiCnt++;
        psList[discIdx].setAll(pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC]);
        psList[discIdx].uiLastCnt = 1;
      }
      else if( i == -1 && defIdx >= 0 )
      {
        psList[defIdx].uiCnt++;
      }
      else if (i == -1)
      {
        psList.push_back(sElement);
        psList[uiIdx].uiCnt++;
        psList[uiIdx].uiLastCnt = discardChroma ? 0 : 1;
        uiIdx++;
      }
    }
  }

  //insertion sort, high frequency -> low frequency
  std::stable_sort(psList.begin(), psList.end());
  UInt uiPaletteSizePrev;
  Pel *pPalettePrev[3];
  for (UInt comp = 0; comp < 3; comp++)
  {
    pPalettePrev[comp] = pcCU->getPalettePred(pcCU, pcCU->getZorderIdxInCtu(), comp, uiPaletteSizePrev);
  }

  if( paletteSize < maxPaletteSizeSPS )
  {
    for (Int i = 0; i < uiIdx; i++)
    {
      Bool includeIntoPalette = true;
      if( (uiScaleX||uiScaleY) && psList[i].uiCnt > 0 && !psList[i].uiLastCnt ) // Find if it can be replaced
      {
        Int bestCand = -1;
        for( UInt uiIdxPrev = 0; uiIdxPrev < uiPaletteSizePrev; uiIdxPrev++ )
        {
          if( psList[i].uiData[0] == pPalettePrev[0][uiIdxPrev] )
          {
            bestCand = uiIdxPrev;
            break;
          }
        }

        if( bestCand != -1 )
        {
          psList[i].uiData[1] = pPalettePrev[1][bestCand];
          psList[i].uiData[2] = pPalettePrev[2][bestCand];
        }
        else if( psList[i].uiCnt < 3 )
          includeIntoPalette = false;
      }
      else if( psList[i].uiCnt == 1 )
      {
        includeIntoPalette = false;
        for( UInt uiIdxPrev = 0; uiIdxPrev < uiPaletteSizePrev; uiIdxPrev++ )
        {
          UInt iCounter = 0;

          for( UInt comp = 0; comp < 3; comp++ )
          {
            if( psList[i].uiData[comp] == pPalettePrev[comp][uiIdxPrev] )
            {
              iCounter++;
            }
            else
            {
              break;
            }
          }
          if( iCounter == 3 )
          {
            includeIntoPalette = true;
            break;
          }
        }
      }

      if( includeIntoPalette && psList[i].uiCnt)
      {
        Palette[0][paletteSize] = psList[i].uiData[0];
        Palette[1][paletteSize] = psList[i].uiData[1];
        Palette[2][paletteSize] = psList[i].uiData[2];
        paletteSize++;
        if (paletteSize == pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize())
        {
          break;
        }
      }
    }
  }
}

Void TComPrediction::calcPixelPred(TComDataCU* pcCU, Pel* pOrg [3], Pel *pPalette[3], Pel* pValue, Pel*paPixelValue[3], Pel * paRecoValue[3], UInt uiWidth, UInt uiHeight,  UInt uiStrideOrg, UInt uiIdx )
{
  Bool bLossless = pcCU->getCUTransquantBypass (0);
  Int iQP[3];
  Int iQPrem[3];
  Int iQPper[3];
  Int quantiserScale[3];
  Int quantiserRightShift[3];
  Int rightShiftOffset[3];
  Int InvquantiserRightShift[3];
  Int iAdd[3];
  for (UInt ch = 0; ch < MAX_NUM_COMPONENT; ch++)
  {
    assert(!pcCU->getColourTransform(0));
    QpParam cQP(*pcCU, ComponentID(ch), 0);
    iQP[ch] = cQP.Qp;
    iQPrem[ch] = iQP[ch] % 6;
    iQPper[ch] = iQP[ch] / 6;
    quantiserScale[ch] = g_quantScales[iQPrem[ch]];
    quantiserRightShift[ch] = QUANT_SHIFT + iQPper[ch];
    rightShiftOffset[ch] = 1 << (quantiserRightShift[ch] - 1);
    InvquantiserRightShift[ch] = IQUANT_SHIFT;
    iAdd[ch] = 1 << (InvquantiserRightShift[ch] - 1);
  }

  UInt uiY, uiX;
  uiY = uiIdx / uiWidth;
  uiX = uiIdx % uiWidth;
  UInt uiScanIdx = uiY * uiStrideOrg + uiX;
  UInt uiYIdxRaster = pcCU->getPaletteScanRotationModeFlag(0)? (uiX * uiStrideOrg + uiY) : (uiY * uiStrideOrg + uiX);
  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);
  UInt uiXC, uiYC, uiScanIdxC, uiYIdxRasterC;
  if(!pcCU->getPaletteScanRotationModeFlag(0))
  {
    uiXC = (uiX>>uiScaleX);
    uiYC = (uiY>>uiScaleY);
    uiScanIdxC = uiYC * (uiStrideOrg>>uiScaleX) + uiXC;
    uiYIdxRasterC = uiYC * (uiStrideOrg>>uiScaleX) + uiXC;
  }
  else
  {
    uiXC = (uiX>>uiScaleY);
    uiYC = (uiY>>uiScaleX);
    uiScanIdxC = uiYC * (uiStrideOrg>>uiScaleY) + uiXC;
    uiYIdxRasterC = uiXC * (uiStrideOrg>>uiScaleX) + uiYC;
  }

  if (bLossless)
  {
    for (UInt ch = 0; ch < MAX_NUM_COMPONENT; ch ++)
    {
      if( ch == 0 )
      {
        paPixelValue[ch][uiScanIdx] =  pOrg[ch][uiYIdxRaster];
        paRecoValue[ch][uiYIdxRaster] = pOrg[ch][uiYIdxRaster];
      }
      else
      {
        if(   pcCU->getPic()->getChromaFormat() == CHROMA_444 ||
            ( pcCU->getPic()->getChromaFormat() == CHROMA_420 && ((uiX&1) == 0) && ((uiY&1) == 0) ) ||
            ( pcCU->getPic()->getChromaFormat() == CHROMA_422 && ((!pcCU->getPaletteScanRotationModeFlag(0) && ((uiX&1) == 0)) || (pcCU->getPaletteScanRotationModeFlag(0) && ((uiY&1) == 0))) )
          )
        {
          paPixelValue[ch][uiScanIdxC] =  pOrg[ch][uiYIdxRasterC];
          paRecoValue[ch][uiYIdxRasterC] = pOrg[ch][uiYIdxRasterC];
        }
      }
    }
  }
  else
  {
    BitDepths bitDepths = pcCU->getSlice()->getSPS()->getBitDepths();
    for (UInt ch = 0; ch < MAX_NUM_COMPONENT; ch ++)
    {
      if( ch == 0 )
      {
        paPixelValue[ch][uiScanIdx] = Pel(max<Int>( 0, ((pOrg[ch][uiYIdxRaster] * quantiserScale[ch] + rightShiftOffset[ch]) >> quantiserRightShift[ch]) ));

        assert( paPixelValue[ch][uiScanIdx] < ( 1 << ( pcCU->getSlice()->getSPS()->getBitDepth( CHANNEL_TYPE_LUMA ) + 1 ) ) );
        paRecoValue[ch][uiYIdxRaster] = (((paPixelValue[ch][uiScanIdx]*g_invQuantScales[iQPrem[ch]])<<iQPper[ch]) + iAdd[ch])>>InvquantiserRightShift[ch];
        paRecoValue[ch][uiYIdxRaster] = Pel(ClipBD<Int>(paRecoValue[ch][uiYIdxRaster], bitDepths.recon[ch? 1:0]));
      }
      else
      {
        if(   pcCU->getPic()->getChromaFormat() == CHROMA_444 ||
            ( pcCU->getPic()->getChromaFormat() == CHROMA_420 && ((uiX&1) == 0) && ((uiY&1) == 0)) ||
            ( pcCU->getPic()->getChromaFormat() == CHROMA_422 && ((!pcCU->getPaletteScanRotationModeFlag(0) && ((uiX&1) == 0)) || (pcCU->getPaletteScanRotationModeFlag(0) && ((uiY&1) == 0))) )
          )
        {
          paPixelValue[ch][uiScanIdxC] = Pel(max<Int>( 0, ((pOrg[ch][uiYIdxRasterC] * quantiserScale[ch] + rightShiftOffset[ch]) >> quantiserRightShift[ch]) ));

          assert( paPixelValue[ch][uiScanIdxC] < ( 1 << ( pcCU->getSlice()->getSPS()->getBitDepth( CHANNEL_TYPE_CHROMA ) + 1 ) ) );
          paRecoValue[ch][uiYIdxRasterC] = (((paPixelValue[ch][uiScanIdxC]*g_invQuantScales[iQPrem[ch]])<<iQPper[ch]) + iAdd[ch])>>InvquantiserRightShift[ch];
          paRecoValue[ch][uiYIdxRasterC] = Pel(ClipBD<Int>(paRecoValue[ch][uiYIdxRasterC], bitDepths.recon[ch? 1:0]));
        }
      }
    }
  }
}

UInt TComPrediction::getTruncBinBits(UInt uiSymbol, UInt uiMaxSymbol)
{
  UInt uiIdxCodeBit = 0;
  UInt uiThresh;
  if (uiMaxSymbol > 256)
  {
    UInt uiThreshVal = 1 << 8;
    uiThresh = 8;
    while (uiThreshVal <= uiMaxSymbol)
    {
      uiThresh++;
      uiThreshVal <<= 1;
    }
    uiThresh--;
  }
  else
  {
    uiThresh = g_uhPaletteTBC[uiMaxSymbol];
  }

  UInt uiVal = 1 << uiThresh;
  assert(uiVal <= uiMaxSymbol);
  assert((uiVal << 1) > uiMaxSymbol);
  assert(uiSymbol < uiMaxSymbol);
  UInt b = uiMaxSymbol - uiVal;
  assert(b < uiVal);
  if (uiSymbol < uiVal - b)
  {
    uiIdxCodeBit = uiThresh;
  }
  else
  {
    uiIdxCodeBit = uiThresh+1;
  }
  return uiIdxCodeBit;
}

UInt TComPrediction::getEpExGolombNumBins(UInt uiSymbol, UInt uiCount)
{
  //UInt bins = 0;
  UInt numBins = 0;

  while( uiSymbol >= (UInt)(1<<uiCount) )
  {
    //bins = 2 * bins + 1;
    numBins++;
    uiSymbol -= 1 << uiCount;
    uiCount++;
  }
  //bins = 2 * bins + 0;
  numBins++;

  //bins = (bins << uiCount) | uiSymbol;
  numBins += uiCount;

  assert( numBins <= 32 );

  return numBins;
}

Double TComPrediction::calcPixelPredRD(TComDataCU* pcCU, Pel pOrg[3], TComRdCost *pcCost, UInt *error, Bool discardChroma)
{
  Pel paPixelValue[3], paRecoValue[3]; 
  Int iQPcurr=Int(pcCU->getQP(0));

  Double rdCost = 0;
  UInt rdError = 0;
  if (pcCU->getCUTransquantBypass(0))
  {
    for (UInt ch = 0; ch < MAX_NUM_COMPONENT; ch++)
    {
      Int bitDepth = pcCU->getSlice()->getSPS()->getBitDepth(ch > 0 ? CHANNEL_TYPE_CHROMA : CHANNEL_TYPE_LUMA);
      rdCost += bitDepth;
    }
  }
  else
  {
    if (iQPcurr != m_prevQP)
    {
      m_prevQP = iQPcurr;
      for (UInt ch = 0; ch < MAX_NUM_COMPONENT; ch++)
      {
        Int iQP[3];
        Int iQPrem[3];
        Int iQPper[3];
        QpParam cQP(*pcCU, ComponentID(ch),0);
        iQP[ch] = cQP.Qp;
        iQPrem[ch] = iQP[ch] % 6;
        iQPper[ch] = iQP[ch] / 6;
        m_quantiserScale[ch] = g_quantScales[iQPrem[ch]];
        m_quantiserRightShift[ch] = QUANT_SHIFT + iQPper[ch];
        m_rightShiftOffset[ch] = 1 << (m_quantiserRightShift[ch] - 1);

        m_invQuantScales[ch]=g_invQuantScales[iQPrem[ch]];
        m_iQPper[ch]=iQPper[ch];

        m_uiMaxVal[ch] = pcCU->xCalcMaxVals(pcCU, ComponentID(ch));
      }
    }

    BitDepths bitDepths = pcCU->getSlice()->getSPS()->getBitDepths();
    for (UInt ch = 0; ch < (discardChroma ? 1 : MAX_NUM_COMPONENT); ch ++)
    {
      paPixelValue[ch] = Pel(Clip3<Int>( 0, m_uiMaxVal[ch], ((pOrg[ch] * m_quantiserScale[ch] + m_rightShiftOffset[ch]) >> m_quantiserRightShift[ch]) ));
      paRecoValue[ch]= (((paPixelValue[ch]*m_invQuantScales[ch])<<m_iQPper[ch]) + 32)>>IQUANT_SHIFT;

      ChannelType comp = ch ? CHANNEL_TYPE_CHROMA : CHANNEL_TYPE_LUMA;
      paRecoValue[ch] = Pel(ClipBD<Int>(paRecoValue[ch], bitDepths.recon[comp]));

      Int iTemp = pOrg[ch] - paRecoValue[ch];
      rdError += (iTemp * iTemp) >> (DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[comp] - 8) << 1);
      rdCost += pcCost->getLambda() * m_escapeNumBins[paPixelValue[ch]];
    }
  }

  *error = rdError;
  rdCost += (*error);
  return (rdCost);
}

Bool TComPrediction::calLeftRun(TComDataCU* pcCU, Pel* pValue, UChar* pSPoint, UInt uiStartPos, UInt uiTotal, UInt &uiRun, UChar* pEscapeFlag)
{
  UInt uiIdx = uiStartPos;
  Pel *pcIndexBlock = m_cIndexBlock;
  while (uiIdx < uiTotal)
  {
    UInt uiTraIdx = m_puiScanOrder[uiIdx];  //unified position variable (raster scan)
    pValue[uiTraIdx] = pcIndexBlock[uiTraIdx] < 0 ? pcIndexBlock[uiTraIdx] + pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize() : pcIndexBlock[uiTraIdx];
    Bool bMismatch = (pcIndexBlock[uiTraIdx] < 0);

    pSPoint[uiTraIdx] = PALETTE_RUN_LEFT;
    pEscapeFlag[uiTraIdx] = (pcIndexBlock[uiTraIdx] < 0)? 1: 0;
    UInt leftTraIdx = uiIdx ? m_puiScanOrder[uiIdx - 1] : 0;
    if( uiIdx > uiStartPos &&
      ( ( pcIndexBlock[leftTraIdx] >= 0 && pValue[uiTraIdx] == pValue[leftTraIdx] && !bMismatch ) || ( bMismatch && pcIndexBlock[leftTraIdx] < 0 ) )
      )
    {
      uiRun++;
    }
    else if (uiIdx > uiStartPos)
    {
      break;
    }
    uiIdx++;
  }
  return true;
}

Bool  TComPrediction::calAboveRun(TComDataCU* pcCU, Pel* pValue, UChar* pSPoint, UInt uiWidth, UInt uiStartPos, UInt uiTotal, UInt &uiRun, UChar* pEscapeFlag)
{
  UInt uiIdx = uiStartPos;
  UInt uiY = 0;
  Bool valid = false;
  Pel *pcIndexBlock = m_cIndexBlock;
  UInt uiTraIdx = m_puiScanOrder[uiIdx];  //unified position variable (raster scan)

  uiY = uiTraIdx / uiWidth;
  if( uiY == 0 )
  {
    return false;
  }

  while (uiIdx < uiTotal)
  {
    UInt uiStride = uiWidth;
    uiTraIdx = m_puiScanOrder[uiIdx];  //unified position variable (raster scan)

    pValue[uiTraIdx] = pcIndexBlock[uiTraIdx] < 0 ? pcIndexBlock[uiTraIdx] + pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize() : pcIndexBlock[uiTraIdx];
    Bool bMismatch = (pcIndexBlock[uiTraIdx] < 0);

    pSPoint[uiTraIdx] = PALETTE_RUN_ABOVE;
    pEscapeFlag[uiTraIdx] = (pcIndexBlock[uiTraIdx] < 0)? 1: 0;

    if ( ( pcIndexBlock[uiTraIdx - uiStride] >= 0 && pValue[uiTraIdx] == pValue[uiTraIdx - uiStride] && !bMismatch ) ||
         ( bMismatch && pcIndexBlock[uiTraIdx - uiStride] < 0 )
       )
    {
      uiRun++;
      valid = true;
    }
    else
    {
      break;
    }
    uiIdx++;
  }
  return valid;
}

Void  TComPrediction::rotationScan( Pel* pLevel, UInt uiWidth, UInt uiHeight, Bool isInverse )
{
  Pel tmpLevel;
  UInt uiPos = 0;
  UInt* puiScanOrder = g_scanOrder[SCAN_UNGROUPED][SCAN_VER][g_aucConvertToBit[uiWidth] + 2][g_aucConvertToBit[uiHeight] + 2];

  for (UInt j = 1; j < uiHeight; j++)
  {
    uiPos += j;
    for (UInt i = j; i < uiWidth; i++)
    {
      tmpLevel = pLevel[uiPos];
      pLevel[uiPos] = pLevel[puiScanOrder[uiPos]];
      pLevel[puiScanOrder[uiPos]] = tmpLevel;
      uiPos++;
    }
  }
}

Void TComPrediction::derivePaletteLossyForcePrediction(TComDataCU *pcCU, Pel *Palette[3], Pel *pSrc[3], UInt uiWidth, UInt uiHeight, UInt uiStride, UInt &paletteSize, TComRdCost *pcCost)
{
  const Int iErrorLimit = getPaletteErrLimit();
  const UInt maxPaletteSizeSPS = pcCU->getSlice()->getSPS()->getSpsScreenExtension().getPaletteMaxSize();
  const UInt uiTotalSize = uiHeight * uiWidth;
  SortingElement *psList = new SortingElement[uiTotalSize];
  SortingElement sElement;
  SortingElement *pListSort = new SortingElement[maxPaletteSizeSPS + 1];

  paletteSize = 0;
  UInt uiIdx = 0, uiPos, uiBestIdx = 0;
  Int last = -1;

  UInt palettePredIndexUsed[MAX_PALETTE_PRED_SIZE];
  memset( palettePredIndexUsed, 0, sizeof(palettePredIndexUsed) );

  UChar paletteIndexUsed[MAX_PALETTE_PRED_SIZE];
  memset( paletteIndexUsed, 0, sizeof(paletteIndexUsed) );

  Pel *pPred[3] = { pcCU->getLastPaletteInLcuFinal(0), pcCU->getLastPaletteInLcuFinal(1), pcCU->getLastPaletteInLcuFinal(2) };

  UInt uiScaleX = pcCU->getPic()->getComponentScaleX(COMPONENT_Cb);
  UInt uiScaleY = pcCU->getPic()->getComponentScaleY(COMPONENT_Cb);

  const BitDepths bitDepths = pcCU->getSlice()->getSPS()->getBitDepths();
  for( UInt uiY = 0; uiY < uiHeight; uiY++ )
  {
    for( UInt uiX = 0; uiX < uiWidth; uiX++ )
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);
      UInt paletteIdx = 0;
      UInt uiMinError = MAX_UINT;
      while( paletteIdx < pcCU->getLastPaletteInLcuSizeFinal(0) )
      {
        UInt uiAbsError = (abs(pPred[0][paletteIdx] - pSrc[0][uiPos]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA] - 8))
                        + (abs(pPred[1][paletteIdx] - pSrc[1][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8))
                        + (abs(pPred[2][paletteIdx] - pSrc[2][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8));

        if( uiAbsError < uiMinError )
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }

      if( uiMinError <= iErrorLimit )
      {
        palettePredIndexUsed[uiBestIdx]++;
      }
    }
  }

  while( uiIdx < maxPaletteSizeSPS )
  {
    UInt maxNoIndexUsed = 0, bestIndex = 0;
    for( UInt i = 0; i < pcCU->getLastPaletteInLcuSizeFinal(0); i++ )
    {
      if( paletteIndexUsed[i] == 0 && palettePredIndexUsed[i] > maxNoIndexUsed )
      {
        maxNoIndexUsed = palettePredIndexUsed[i];
        bestIndex = i;
      }
    }
    if( maxNoIndexUsed > 0 )
    {
      paletteIndexUsed[bestIndex] = 1;

      Palette[0][paletteSize] = pPred[0][bestIndex];
      Palette[1][paletteSize] = pPred[1][bestIndex];
      Palette[2][paletteSize] = pPred[2][bestIndex];
      paletteSize++;
    }
    else
    {
      break;
    }
    uiIdx++;
  }

  uiIdx = 0;
  for( UInt uiY = 0; uiY < uiHeight; uiY++ )
  {
    for( UInt uiX = 0; uiX < uiWidth; uiX++ )
    {
      uiPos = uiY * uiWidth + uiX;
      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

      UInt paletteIdx = 0;
      UInt uiMinError = MAX_UINT;
      while( paletteIdx < paletteSize )
      {
        UInt uiAbsError = (abs(Palette[0][paletteIdx] - pSrc[0][uiPos]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA] - 8))
                        + (abs(Palette[1][paletteIdx] - pSrc[1][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8))
                        + (abs(Palette[2][paletteIdx] - pSrc[2][uiPosC]) >> DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8));

        if (uiAbsError < uiMinError)
        {
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }

      if( uiMinError > iErrorLimit )
      {
        sElement.setAll(pSrc[0][uiPos], pSrc[1][uiPosC], pSrc[2][uiPosC]);
        Int besti = last, bestSAD = (last == -1) ? MAX_UINT : psList[last].getSAD(sElement, pcCU->getSlice()->getSPS()->getBitDepths());
        if (bestSAD)
        {
          for (Int i = uiIdx - 1; i >= 0; i--)
          {
            UInt sad = psList[i].getSAD(sElement, pcCU->getSlice()->getSPS()->getBitDepths());
            if (sad < bestSAD)
            {
              bestSAD = sad;
              besti = i;
              if (!sad)
              {
                break;
              }
            }
          }
        }

        if( besti >= 0 && psList[besti].almostEqualData(sElement, iErrorLimit, pcCU->getSlice()->getSPS()->getBitDepths()) )
        {
          psList[besti].addElement(sElement);
          last = besti;
        }
        else
        {
          psList[uiIdx].copyDataFrom(sElement);
          psList[uiIdx].uiCnt = 1;
          last = uiIdx;
          uiIdx++;
        }
      }
    }
  }

  for( Int i = 0; i < maxPaletteSizeSPS; i++ )
  {
    pListSort[i].uiCnt = 0;
    pListSort[i].setAll(0, 0, 0);
  }

  //bubble sorting
  UInt uiDictMaxSize = 1;
  for( Int i = 0; i < uiIdx; i++ )
  {
    if( psList[i].uiCnt > pListSort[uiDictMaxSize - 1].uiCnt )
    {
      Int j;
      for( j = uiDictMaxSize; j > 0; j-- )
      {
        if( psList[i].uiCnt > pListSort[j - 1].uiCnt )
        {
          pListSort[j].copyAllFrom(pListSort[j - 1]);
          uiDictMaxSize = std::min(uiDictMaxSize + 1, maxPaletteSizeSPS);
        }
        else
        {
          break;
        }
      }
      pListSort[j].copyAllFrom(psList[i]);
    }
  }

  UInt uiNumTotalBits = pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) + (pcCU->getSlice()->getSPS()->getBitDepth(CHANNEL_TYPE_CHROMA)<<1);
  Double bitCost = pcCost->getLambda() * uiNumTotalBits;

  for( Int i = 0; i < maxPaletteSizeSPS && paletteSize < maxPaletteSizeSPS; i++ )
  {
    if( pListSort[i].uiCnt )
    {
      Int iHalf = pListSort[i].uiCnt >> 1;
      Palette[0][paletteSize] = (pListSort[i].uiSumData[0] + iHalf) / pListSort[i].uiCnt;
      Palette[1][paletteSize] = (pListSort[i].uiSumData[1] + iHalf) / pListSort[i].uiCnt;
      Palette[2][paletteSize] = (pListSort[i].uiSumData[2] + iHalf) / pListSort[i].uiCnt;

      Bool bDuplicate = false;
      if( pListSort[i].uiCnt == 1 )
      {
        bDuplicate = true;
      }
      else
      {
        Int best = -1;
        if( iErrorLimit )
        {  
          Double pal[3] = { pListSort[i].uiSumData[0] / (Double)pListSort[i].uiCnt,
                            pListSort[i].uiSumData[1] / (Double)pListSort[i].uiCnt,
                            pListSort[i].uiSumData[2] / (Double)pListSort[i].uiCnt };

          Double err = pal[0] - Palette[0][paletteSize];
          Double bestCost = (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA] - 8)));
          err = pal[1] - Palette[1][paletteSize]; bestCost += (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8)));
          err = pal[2] - Palette[2][paletteSize]; bestCost += (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8)));
          bestCost = bestCost * pListSort[i].uiCnt + bitCost;

          for( Int t = 0; t < paletteSize; t++ )
          {
            if( Palette[0][paletteSize] == Palette[0][t] && Palette[1][paletteSize] == Palette[1][t] && Palette[2][paletteSize] == Palette[2][t] )
            {
              bDuplicate = true;
              break;
            }

            err = pal[0] - Palette[0][t];
            Double cost = (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_LUMA] - 8)));
            err = pal[1] - Palette[1][t]; cost += (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8)));
            err = pal[2] - Palette[2][t]; cost += (err*err) / (1 << (2 * DISTORTION_PRECISION_ADJUSTMENT(bitDepths.recon[CHANNEL_TYPE_CHROMA] - 8)));
            cost *= pListSort[i].uiCnt;
            if( cost < bestCost )
            {
              best = t;
              bestCost = cost;
            }
          }
          if( best != -1 )
          {
            bDuplicate = true;
          }
        }
      }

      if( !bDuplicate )
      {
        paletteSize++;
      }
    }
    else
    {
      break;
    }
  }

  UInt palettePredSamples[MAX_PALETTE_SIZE][5];
  memset(palettePredSamples, 0, 5*MAX_PALETTE_SIZE*sizeof(UInt));
  Int iErrorLimitSqr = 3 * getPaletteErrLimit()*getPaletteErrLimit();

  for (UInt uiY = 0; uiY < uiHeight; uiY++)
  {
    for (UInt uiX = 0; uiX < uiWidth; uiX++)
    {
      Bool discardChroma = uiY&uiScaleY || uiX&uiScaleX;
      uiPos = uiY * uiWidth + uiX;

      UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

      UInt paletteIdx = 0;
      UInt uiMinError = MAX_UINT;
      while (paletteIdx < paletteSize)
      {
        Int iTemp=Palette[0][paletteIdx] - pSrc[0][uiPos];
        UInt uiAbsError = (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
        iTemp=Palette[1][paletteIdx] - pSrc[1][uiPosC];
        uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
        iTemp=Palette[2][paletteIdx] - pSrc[2][uiPosC];
        uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));

        if (uiAbsError < uiMinError)
        {
          uiBestIdx = paletteIdx;
          uiMinError = uiAbsError;
          if (uiMinError == 0)
          {
            break;
          }
        }
        paletteIdx++;
      }

      UInt escape=0;
      if (uiMinError > iErrorLimitSqr)
      {

        Pel pOrg[3]={ pSrc[0][uiPos],  pSrc[1][uiPosC],  pSrc[2][uiPosC]};
        UInt errorTemp; 
        Double rdCost=calcPixelPredRD(pcCU, pOrg, pcCost, &errorTemp); 
        if (rdCost<uiMinError) 
        {
          escape=1;
        }
      }

      if (escape==0)
      {
        palettePredSamples[uiBestIdx][0]++;
        palettePredSamples[uiBestIdx][1] += pSrc[0][uiPos];
        if (!discardChroma)
        {
          palettePredSamples[uiBestIdx][2] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += SCM_V0034_PALETTE_CHROMA_SETTINGS*pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4] += SCM_V0034_PALETTE_CHROMA_SETTINGS;
        }
        else
        {
          palettePredSamples[uiBestIdx][2] += pSrc[1][uiPosC];
          palettePredSamples[uiBestIdx][3] += pSrc[2][uiPosC];
          palettePredSamples[uiBestIdx][4]++;
        }
        m_cIndexBlock[uiPos] = uiBestIdx;
      }
      else
      {
        m_cIndexBlock[uiPos]=-1;
      }
    }
  }

  UInt paletteIndBest[MAX_PALETTE_PRED_SIZE];


  UInt paletteSizeTemp=0;
  for (Int i = 0; i < paletteSize; i++)
  {
    if(palettePredSamples[i][0] > 0)
    {
      Palette[0][paletteSizeTemp] = (palettePredSamples[i][1]+palettePredSamples[i][0]/2)/palettePredSamples[i][0];
      Palette[1][paletteSizeTemp] = (palettePredSamples[i][2]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];
      Palette[2][paletteSizeTemp] = (palettePredSamples[i][3]+palettePredSamples[i][4]/2)/palettePredSamples[i][4];

      Double uiMinError = pcCost->getLambda()*(pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]+2*pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]), 
        uiAbsError;

      for (UInt uiY = 0; uiY < uiHeight; uiY++)
      {
        for (UInt uiX = 0; uiX < uiWidth; uiX++)
        {
          uiPos = uiY * uiWidth + uiX;
          if (m_cIndexBlock[uiPos]==i)
          {

            UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

            Int iTemp=Palette[0][paletteSizeTemp] - pSrc[0][uiPos];
            uiMinError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
            iTemp=Palette[1][paletteSizeTemp] - pSrc[1][uiPosC];
            uiMinError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
            iTemp=Palette[2][paletteSizeTemp] - pSrc[2][uiPosC];
            uiMinError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
          }
        }
      }


      UInt maxPredCheck=findCandidatePalettePredictors(paletteIndBest, pcCU, Palette, pPred, paletteSizeTemp, MAX_PRED_CHEK);
      Int best=-1;
      UInt testedPalettePred;

      for(int t=0; t<maxPredCheck; t++)
      {
        testedPalettePred=paletteIndBest[t];

        uiAbsError=0;
        for (UInt uiY = 0; uiY < uiHeight; uiY++)
        {
          for (UInt uiX = 0; uiX < uiWidth; uiX++)
          {
            uiPos = uiY * uiWidth + uiX;
            if (m_cIndexBlock[uiPos]==i)
            {

              UInt uiPosC = (uiY>>uiScaleY) * (uiWidth>>uiScaleX) + (uiX>>uiScaleX);

              Int iTemp=pPred[0][testedPalettePred] - pSrc[0][uiPos];
              uiAbsError += (( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_LUMA]-8) << 1));
              iTemp=pPred[1][testedPalettePred] - pSrc[1][uiPosC];
              uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
              iTemp=pPred[2][testedPalettePred] - pSrc[2][uiPosC];
              uiAbsError+=(( iTemp * iTemp ) >>  DISTORTION_PRECISION_ADJUSTMENT((pcCU->getSlice()->getSPS()->getBitDepths().recon[CHANNEL_TYPE_CHROMA]-8) << 1));
            }
          }
          if (uiAbsError>uiMinError)
          {
            break;
          }
        }

        if (uiAbsError < uiMinError || (uiAbsError == uiMinError && best>testedPalettePred))
        {
          best = testedPalettePred;
          uiMinError = uiAbsError;
        }
      }

      if( best != -1 )
      {
        Palette[0][paletteSizeTemp] = pPred[0][best];
        Palette[1][paletteSizeTemp] = pPred[1][best];
        Palette[2][paletteSizeTemp] = pPred[2][best];
      }


      Bool bDuplicate = false;
      if( palettePredSamples[i][0] == 1 && best == -1 )
      {
        bDuplicate = true;
      }
      else
      {
        for( Int t=0; t<paletteSizeTemp; t++)
        {
          if( Palette[0][paletteSizeTemp] == Palette[0][t] && Palette[1][paletteSizeTemp] == Palette[1][t] && Palette[2][paletteSizeTemp] == Palette[2][t] )
          {
            bDuplicate = true;
            break;
          }
        }
      }
      if( !bDuplicate ) paletteSizeTemp++;
    }
  }

  paletteSize=paletteSizeTemp;

  delete[] psList;
  delete[] pListSort;
}

//! \}
