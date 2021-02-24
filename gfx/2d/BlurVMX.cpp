/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Blur.h"

#include <altivec.h>
#include <string.h>

namespace mozilla {
namespace gfx {

MOZ_ALWAYS_INLINE
vector float Reciprocal(vector float v)
{
  //Get the reciprocal estimate
  vector float estimate = vec_re(v);

  //One round of Newton-Raphson refinement
  return vec_madd(vec_nmsub(estimate, v, vec_ctf(vec_splat_u32(1), 0)), estimate, estimate );
}

MOZ_ALWAYS_INLINE
vector unsigned int DivideArray(vector unsigned int data, vector float aDivisor)
{
  return vec_ctu(vec_madd(vec_ctf(data, 0), aDivisor, (vector float)vec_splat_u32(0)), 0);
}

MOZ_ALWAYS_INLINE
uint32_t DivideAndPack(vector unsigned int aValues, vector float aDivisor)
{
  uint32_t retVal __attribute__((aligned(16)));

  vector unsigned int temp2 = (vector unsigned int)vec_packsu(vec_packsu(DivideArray(aValues, aDivisor), vec_splat_u32(0)), (vector unsigned short)vec_splat_u32(0));

  vec_ste(temp2, 0, &retVal);

  return retVal;
}

MOZ_ALWAYS_INLINE
void LoadIntegralRowFromRow(uint32_t* aDest, const uint8_t* aSource,
                            int32_t aSourceWidth, int32_t aLeftInflation,
                            int32_t aRightInflation)
{
  int32_t currentRowSum = 0;

  for (int x = 0; x < aLeftInflation; x++) {
    currentRowSum += aSource[0];
    aDest[x] = currentRowSum;
  }
  for (int x = aLeftInflation; x < (aSourceWidth + aLeftInflation); x++) {
    currentRowSum += aSource[(x - aLeftInflation)];
    aDest[x] = currentRowSum;
  }
  for (int x = (aSourceWidth + aLeftInflation); x < (aSourceWidth + aLeftInflation + aRightInflation); x++) {
    currentRowSum += aSource[aSourceWidth - 1];
    aDest[x] = currentRowSum;
  }
}

// This function calculates an integral of four pixels stored in the 4
// 32-bit integers on aPixels. i.e. for { 30, 50, 80, 100 } this returns
// { 30, 80, 160, 260 }.
MOZ_ALWAYS_INLINE
vector unsigned int AccumulatePixelSums(vector unsigned int aPixels)
{
  vector unsigned char shiftCounter = vec_sll(vec_splat_u8(8), vec_splat_u8(2));
  vector unsigned int currentPixels = vec_sro(aPixels, shiftCounter);
  vector unsigned int sumPixels = vec_add(aPixels, currentPixels);

  shiftCounter = vec_sll(shiftCounter, vec_splat_u8(1));
  currentPixels = vec_sro(sumPixels, shiftCounter);
  sumPixels = vec_add(sumPixels, currentPixels);

  return sumPixels;
}

MOZ_ALWAYS_INLINE
vector unsigned short AccumulatePixelSums(vector unsigned short aPixels)
{
  vector unsigned char shiftCounter = vec_sll(vec_splat_u8(8), vec_splat_u8(1));
  vector unsigned short currentPixels = vec_sro(aPixels, shiftCounter);
  vector unsigned short sumPixels = vec_add(aPixels, currentPixels);

  shiftCounter = vec_sll(shiftCounter, vec_splat_u8(1));
  currentPixels = vec_sro(sumPixels, shiftCounter);
  sumPixels = vec_add(sumPixels, currentPixels);

  shiftCounter = vec_sll(shiftCounter, vec_splat_u8(1));
  currentPixels = vec_sro(sumPixels, shiftCounter);
  sumPixels = vec_add(sumPixels, currentPixels);

  return sumPixels;
}

MOZ_ALWAYS_INLINE
void GenerateIntegralImage_VMX(int32_t aLeftInflation, int32_t aRightInflation,
                               int32_t aTopInflation, int32_t aBottomInflation,
                               uint32_t* aIntegralImage, size_t aIntegralImageStride,
                               uint8_t* &mData, int32_t &mStride, const IntSize &aSize)
{
  MOZ_ASSERT(!(aLeftInflation & 3));

  uint8_t* aSource = mData;
  int32_t aSourceStride = mStride;

  uint32_t stride32bit = aIntegralImageStride / 4;

  IntSize integralImageSize(aSize.width + aLeftInflation + aRightInflation,
                            aSize.height + aTopInflation + aBottomInflation);

  LoadIntegralRowFromRow(aIntegralImage, aSource, aSize.width, aLeftInflation, aRightInflation);

  for (int y = 1; y < aTopInflation + 1; y++) {
    uint32_t* intRow = aIntegralImage + (y * stride32bit);
    uint32_t* intPrevRow = aIntegralImage + (y - 1) * stride32bit;
    uint32_t* intFirstRow = aIntegralImage;

    int x;
    for (x = 0; (x += 16) < (integralImageSize.width * 4); x += 16) {
      vector unsigned int firstRow = vec_ld(x - 16, intFirstRow);
      vector unsigned int previousRow = vec_ld(x - 16, intPrevRow);
      vec_st(vec_add(firstRow, previousRow), x - 16, intRow);

      firstRow = vec_ld(x, intFirstRow);
      previousRow = vec_ld(x, intPrevRow);
      vec_st(vec_add(firstRow, previousRow), x, intRow);
    }
    if ((x -= 16) < (integralImageSize.width * 4)) {
      vector unsigned int firstRow = vec_ld(x, intFirstRow);
      vector unsigned int previousRow = vec_ld(x, intPrevRow);
      vec_st(vec_add(firstRow, previousRow), x, intRow);
   }
  }

  for (int y = aTopInflation + 1; y < (aSize.height + aTopInflation); y++) {
    vector unsigned int currentRowSum = vec_splat_u32(0);
    uint32_t* intRow = aIntegralImage + (y * stride32bit);
    uint32_t* intPrevRow = aIntegralImage + (y - 1) * stride32bit;
    uint8_t* sourceRow = aSource + aSourceStride * (y - aTopInflation);

    volatile uint32_t pixel __attribute__((aligned(16))) = sourceRow[0];

    int x;
    volatile vector unsigned int sumPixels0 = AccumulatePixelSums(vec_splat(vec_lde(0, &pixel), 0));
    for (x = 0; (x += 16) < (aLeftInflation * 4); x += 16) {
      vector unsigned int sumPixels = vec_add(sumPixels0, currentRowSum);

      currentRowSum = vec_splat(sumPixels, 3);

      vec_st(vec_add(sumPixels, vec_ld(x - 16, intPrevRow)), x - 16, intRow);

      sumPixels = vec_add(sumPixels0, currentRowSum);
      currentRowSum = vec_splat(sumPixels, 3);
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }
    if ((x -= 16) < (aLeftInflation * 4)) {
      vector unsigned int sumPixels = vec_add(sumPixels0, currentRowSum);
      currentRowSum = vec_splat(sumPixels, 3);
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }

    for (x = (aLeftInflation * 4); (x += 48) < ((aSize.width + aLeftInflation) * 4); x += 16) {
      uint64_t pixels[2] __attribute__((aligned(16))) = {*(uint64_t*)(sourceRow + (((x - 48) / 4) - aLeftInflation)),
                                                         *(uint64_t*)(sourceRow + (((x - 16) / 4) - aLeftInflation))};
      vector unsigned char pixelsVector = vec_ld(0, (unsigned char*)&pixels);
      // It's important to shuffle here. When we exit this loop currentRowSum
      // has to be set to sumPixels, so that the following loop can get the
      // correct pixel for the currentRowSum. The highest order pixel in
      // currentRowSum could've originated from accumulation in the stride.
      currentRowSum = vec_splat(currentRowSum, 3);

      vector unsigned short sumPixelsShort = AccumulatePixelSums((vector unsigned short)vec_mergeh((vector unsigned char)vec_splat_u32(0), pixelsVector));
      vector unsigned int sumPixels = vec_add((vector unsigned int)vec_mergeh((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);

      vec_st(vec_add(sumPixels, vec_ld(x - 48, intPrevRow)), x - 48, intRow);

      sumPixels = vec_add((vector unsigned int)vec_mergel((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);
      vec_st(vec_add(sumPixels, vec_ld(x - 32, intPrevRow)), x - 32, intRow);

      currentRowSum = vec_splat(sumPixels, 3);

      sumPixelsShort = AccumulatePixelSums((vector unsigned short)vec_mergel((vector unsigned char)vec_splat_u32(0), pixelsVector));
      sumPixels = vec_add((vector unsigned int)vec_mergeh((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);

      vec_st(vec_add(sumPixels, vec_ld(x - 16, intPrevRow)), x - 16, intRow);

      sumPixels = vec_add((vector unsigned int)vec_mergel((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);
      currentRowSum = sumPixels;
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }
    if ((x -= 32) < ((aSize.width + aLeftInflation) * 4)) {
      uint64_t pixels __attribute__((aligned(16))) = *(uint64_t*)(sourceRow + (((x - 16) / 4) - aLeftInflation));
      // It's important to shuffle here. When we exit this loop currentRowSum
      // has to be set to sumPixels, so that the following loop can get the
      // correct pixel for the currentRowSum. The highest order pixel in
      // currentRowSum could've originated from accumulation in the stride.
      currentRowSum = vec_splat(currentRowSum, 3);

      vector unsigned short sumPixelsShort = AccumulatePixelSums((vector unsigned short)vec_mergeh((vector unsigned char)vec_splat_u32(0), vec_ld(0, (unsigned char*)&pixels)));
      vector unsigned int sumPixels = vec_add((vector unsigned int)vec_mergeh((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);

      vec_st(vec_add(sumPixels, vec_ld(x - 16, intPrevRow)), x - 16, intRow);

      sumPixels = vec_add((vector unsigned int)vec_mergel((vector unsigned short)vec_splat_u32(0), sumPixelsShort), currentRowSum);
      currentRowSum = sumPixels;
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
      x += 32;
    }
    if ((x -= 16) < ((aSize.width + aLeftInflation) * 4)) {
      uint32_t pixels __attribute__((aligned(16))) = *(uint32_t*)(sourceRow + ((x / 4) - aLeftInflation));
      currentRowSum = vec_splat(currentRowSum, 3);
      vector unsigned int sumPixels = AccumulatePixelSums((vector unsigned int)vec_mergeh((vector unsigned short)vec_splat_u32(0), (vector unsigned short)vec_mergeh((vector unsigned char)vec_splat_u32(0), (vector unsigned char)vec_lde(0, &pixels))));
      sumPixels = vec_add(sumPixels, currentRowSum);
      currentRowSum = sumPixels;
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }

    pixel = sourceRow[aSize.width - 1];
    x = (aSize.width + aLeftInflation);
    if ((aSize.width & 3)) {
      // Deal with unaligned portion. Get the correct pixel from currentRowSum,
      // see explanation above.
      volatile uint32_t __attribute__((aligned(16))) intCurrentRowSum = ((uint32_t*)&currentRowSum)[(aSize.width % 4) - 1];
      for (; x < integralImageSize.width; x++) {
        // We could be unaligned here!
        if (!(x & 3)) {
          // aligned!
          currentRowSum = vec_splat(vec_lde(0, &intCurrentRowSum), 0);
          break;
        }
        intCurrentRowSum += pixel;
        intRow[x] = intPrevRow[x] + intCurrentRowSum;
      }
    } else {
      currentRowSum = vec_splat(currentRowSum, 3);
    }

    sumPixels0 = AccumulatePixelSums(vec_splat(vec_lde(0, &pixel), 0));
    for (x = x * 4; (x += 16) < (integralImageSize.width * 4); x += 16) {
      vector unsigned int sumPixels = vec_add(sumPixels0, currentRowSum);

      currentRowSum = vec_splat(sumPixels, 3);

      vec_st(vec_add(sumPixels, vec_ld(x - 16, intPrevRow)), x - 16, intRow);

      sumPixels = vec_add(sumPixels0, currentRowSum);
      currentRowSum = vec_splat(sumPixels, 3);
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }
    if ((x -= 16) < (integralImageSize.width * 4)) {
      vector unsigned int sumPixels = vec_add(sumPixels0, currentRowSum);
      currentRowSum = vec_splat(sumPixels, 3);
      vec_st(vec_add(sumPixels, vec_ld(x, intPrevRow)), x, intRow);
    }
  }

  if (aBottomInflation) {
    uint32_t* intLastRow = aIntegralImage + (integralImageSize.height - 1) * stride32bit;
    // Store the last valid row of our source image in the last row of
    // our integral image. This will be overwritten with the correct values
    // in the upcoming loop.
    LoadIntegralRowFromRow(intLastRow,
                           aSource + (aSize.height - 1) * aSourceStride, aSize.width, aLeftInflation, aRightInflation);


    for (int y = aSize.height + aTopInflation; y < integralImageSize.height; y++) {
      uint32_t* intRow = aIntegralImage + (y * stride32bit);
      uint32_t* intPrevRow = aIntegralImage + (y - 1) * stride32bit;

      int x;
      for (x = 0; (x += 16) < (integralImageSize.width * 4); x += 16) {
        vector unsigned int lastRow = vec_ld(x - 16, intLastRow);
        vector unsigned int previousRow = vec_ld(x - 16, intPrevRow);
        vec_st(vec_add(lastRow, previousRow), x - 16, intRow);

        lastRow = vec_ld(x, intLastRow);
        previousRow = vec_ld(x, intPrevRow);
        vec_st(vec_add(lastRow, previousRow), x, intRow);
      }
      if ((x -= 16) < (integralImageSize.width * 4)) {
        vector unsigned int lastRow = vec_ld(x, intLastRow);
        vector unsigned int previousRow = vec_ld(x, intPrevRow);
        vec_st(vec_add(lastRow, previousRow), x, intRow);
      }
    }
  }
}

MOZ_ALWAYS_INLINE
void loop(int32_t startIdx, int32_t endIdx,
          uint32_t* topLeftBase, uint32_t* topRightBase, uint32_t* bottomRightBase, uint32_t* bottomLeftBase,
          uint8_t *data, int32_t stride,
          vector float reciprocal,
          int32_t y)
{
  int topLeftIndex = reinterpret_cast<uint32_t>(topLeftBase + startIdx) & 0xf ? 0 : 1;
  int topRightIndex = reinterpret_cast<uint32_t>(topRightBase + startIdx) & 0xf ? 0 : 1;
  int bottomRightIndex = reinterpret_cast<uint32_t>(bottomRightBase + startIdx) & 0xf ? 0 : 1;
  int bottomLeftIndex = reinterpret_cast<uint32_t>(bottomLeftBase + startIdx) & 0xf ? 0 : 1;

  vector unsigned char topLeftMask = vec_lvsl(0, reinterpret_cast<unsigned char*>(topLeftBase + startIdx));
  vector unsigned int topLeftVector1 = vec_ld(0, topLeftBase + startIdx);
  vector unsigned int topLeftVector2;

  vector unsigned char topRightMask = vec_lvsl(0, reinterpret_cast<unsigned char*>(topRightBase + startIdx));
  vector unsigned int topRightVector1 = vec_ld(0, topRightBase + startIdx);
  vector unsigned int topRightVector2;

  vector unsigned char bottomRightMask = vec_lvsl(0, reinterpret_cast<unsigned char*>(bottomRightBase + startIdx));
  vector unsigned int bottomRightVector1 = vec_ld(0, bottomRightBase + startIdx);
  vector unsigned int bottomRightVector2;

  vector unsigned char bottomLeftMask = vec_lvsl(0, reinterpret_cast<unsigned char*>(bottomLeftBase + startIdx));
  vector unsigned int bottomLeftVector1 = vec_ld(0, bottomLeftBase + startIdx);
  vector unsigned int bottomLeftVector2;

  int32_t x;
  for (x = startIdx; (x += 4) < endIdx; x += 4) {
// Safe to use with aligned and unaligned addresses
#define LoadUnaligned(index, target, MSQ, LSQ , mask) \
({                                                    \
  LSQ = vec_ld(index + 15, target);                   \
  vec_perm(MSQ, LSQ, mask);                           \
})

    vector unsigned int topLeft = LoadUnaligned(topLeftIndex, topLeftBase + x - 4, topLeftVector1, topLeftVector2, topLeftMask);
    topLeftVector1 = topLeftVector2;

    vector unsigned int topRight = LoadUnaligned(topRightIndex, topRightBase + x - 4, topRightVector1, topRightVector2, topRightMask);
    topRightVector1 = topRightVector2;

    vector unsigned int bottomRight = LoadUnaligned(bottomRightIndex, bottomRightBase + x - 4, bottomRightVector1, bottomRightVector2, bottomRightMask);
    bottomRightVector1 = bottomRightVector2;

    vector unsigned int bottomLeft = LoadUnaligned(bottomLeftIndex, bottomLeftBase + x - 4, bottomLeftVector1, bottomLeftVector2, bottomLeftMask);
    bottomLeftVector1 = bottomLeftVector2;

    vector unsigned int values = vec_add(vec_sub(vec_sub(bottomRight, topRight), bottomLeft), topLeft);

    *(uint32_t*)(data + stride * y + x - 4) = DivideAndPack(values, reciprocal);

    topLeft = LoadUnaligned(topLeftIndex, topLeftBase + x, topLeftVector1, topLeftVector2, topLeftMask);
    topLeftVector1 = topLeftVector2;

    topRight = LoadUnaligned(topRightIndex, topRightBase + x, topRightVector1, topRightVector2, topRightMask);
    topRightVector1 = topRightVector2;

    bottomRight = LoadUnaligned(bottomRightIndex, bottomRightBase + x, bottomRightVector1, bottomRightVector2, bottomRightMask);
    bottomRightVector1 = bottomRightVector2;

    bottomLeft = LoadUnaligned(bottomLeftIndex, bottomLeftBase + x, bottomLeftVector1, bottomLeftVector2, bottomLeftMask);
    bottomLeftVector1 = bottomLeftVector2;

    values = vec_add(vec_sub(vec_sub(bottomRight, topRight), bottomLeft), topLeft);

    *(uint32_t*)(data + stride * y + x) = DivideAndPack(values, reciprocal);
  }
  if ((x -= 4) < endIdx) {
    vector unsigned int topLeft = LoadUnaligned(topLeftIndex, topLeftBase + x, topLeftVector1, topLeftVector2, topLeftMask);
    topLeftVector1 = topLeftVector2;

    vector unsigned int topRight = LoadUnaligned(topRightIndex, topRightBase + x, topRightVector1, topRightVector2, topRightMask);
    topRightVector1 = topRightVector2;

    vector unsigned int bottomRight = LoadUnaligned(bottomRightIndex, bottomRightBase + x, bottomRightVector1, bottomRightVector2, bottomRightMask);
    bottomRightVector1 = bottomRightVector2;

    vector unsigned int bottomLeft = LoadUnaligned(bottomLeftIndex, bottomLeftBase + x, bottomLeftVector1, bottomLeftVector2, bottomLeftMask);
    bottomLeftVector1 = bottomLeftVector2;

    vector unsigned int values = vec_add(vec_sub(vec_sub(bottomRight, topRight), bottomLeft), topLeft);

    *(uint32_t*)(data + stride * y + x) = DivideAndPack(values, reciprocal);
  }
}

MOZ_ALWAYS_INLINE
void Blur_VMX(int32_t aLeftLobe, int32_t aRightLobe,
              int32_t aTopLobe, int32_t aBottomLobe,
              uint32_t* aIntegralImage, size_t aIntegralImageStride,
              uint8_t* &aData, int32_t &mStride, const IntSize &size,
              uint32_t* boxSize, int32_t leftInflation,
              IntRect &mSkipRect)
{
  // Storing these locally makes this about 30% faster! Presumably the compiler
  // can't be sure we're not altering the member variables in this loop.
  IntRect skipRect = mSkipRect;
  uint8_t *data = aData;
  int32_t stride = mStride;

  uint32_t stride32bit = aIntegralImageStride / 4;

  // This points to the start of the rectangle within the IntegralImage that overlaps
  // the surface being blurred.
  uint32_t* innerIntegral = aIntegralImage + (aTopLobe * stride32bit) + leftInflation;

  vector unsigned int divisor = vec_splat(vec_lde(0, boxSize), 0);
  vector float reciprocal = vec_re(vec_ctf(divisor, 0));

  for (int32_t y = 0; y < size.height; y++) {
    bool inSkipRectY = y > skipRect.y && y < skipRect.YMost();

    uint32_t* topLeftBase = innerIntegral + ((y - aTopLobe) * ptrdiff_t(stride32bit) - aLeftLobe);
    uint32_t* topRightBase = innerIntegral + ((y - aTopLobe) * ptrdiff_t(stride32bit) + aRightLobe);
    uint32_t* bottomRightBase = innerIntegral + ((y + aBottomLobe) * ptrdiff_t(stride32bit) + aRightLobe);
    uint32_t* bottomLeftBase = innerIntegral + ((y + aBottomLobe) * ptrdiff_t(stride32bit) - aLeftLobe);

    if (inSkipRectY) {
      loop(0, skipRect.x + 4, topLeftBase, topRightBase, bottomRightBase, bottomLeftBase, data, stride, reciprocal,y);
      loop(skipRect.XMost(), size.width, topLeftBase, topRightBase, bottomRightBase, bottomLeftBase, data, stride, reciprocal, y);
    } else {
      loop(0, size.width, topLeftBase, topRightBase, bottomRightBase, bottomLeftBase, data, stride, reciprocal, y);
    }
  }
}

/**
 * Attempt to do an in-place box blur using an integral image.
 */
void
AlphaBoxBlur::BoxBlur_VMX(uint8_t* aData,
			  int32_t aLeftLobe,
                          int32_t aRightLobe,
                          int32_t aTopLobe,
                          int32_t aBottomLobe,
                          uint32_t* aIntegralImage,
                          size_t aIntegralImageStride)
{
  IntSize size = GetSize();

  MOZ_ASSERT(size.height > 0);

  // Our 'left' or 'top' lobe will include the current pixel. i.e. when
  // looking at an integral image the value of a pixel at 'x,y' is calculated
  // using the value of the integral image values above/below that.
  aLeftLobe++;
  aTopLobe++;
  uint32_t boxSize __attribute__((aligned(16))) = (aLeftLobe + aRightLobe) * (aTopLobe + aBottomLobe);

  MOZ_ASSERT(static_cast<int32_t>(boxSize) > 0);

  if (boxSize == 1) {
      return;
  }

  int32_t leftInflation = RoundUpToMultipleOf4(aLeftLobe).value();

  GenerateIntegralImage_VMX(leftInflation, aRightLobe, aTopLobe, aBottomLobe,
                            aIntegralImage, aIntegralImageStride, aData,
                            mStride, size);

  Blur_VMX(aLeftLobe, aRightLobe, aTopLobe, aBottomLobe,
           aIntegralImage, aIntegralImageStride, aData,
           mStride, size, &boxSize, leftInflation, mSkipRect);
}

}
}
