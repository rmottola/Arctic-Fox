/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWEBPDecoder_h__
#define nsWEBPDecoder_h__

#include "Decoder.h"

extern "C" {
#include "webp/decode.h"
}

namespace mozilla {
namespace image {
class RasterImage;

//////////////////////////////////////////////////////////////////////
// nsWEBPDecoder Definition

class nsWEBPDecoder : public Decoder
{
public:
  virtual ~nsWEBPDecoder();

  void InitInternal() override;
  void WriteInternal(const char* aBuffer, uint32_t aCount) override;
  void FinishInternal() override;
private:
  friend class DecoderFactory;

  // Decoders should only be instantiated via DecoderFactory.
  explicit nsWEBPDecoder(RasterImage* aImage);

  WebPIDecoder *mDecoder;
  uint8_t *mData;          // Pointer to WebP-decoded data.
  int mPreviousLastLine;   // Last image scan-line read so far.

};

} // namespace image
} // namespace mozilla

#endif // nsWEBPDecoder_h__
