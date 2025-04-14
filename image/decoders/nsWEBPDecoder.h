/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWEBPDecoder_h__
#define nsWEBPDecoder_h__

#include "Decoder.h"

#include "StreamingLexer.h"

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
  Maybe<TerminalState> DoDecode(SourceBufferIterator& aIterator) override;
  void FinishInternal() override;
private:
  friend class DecoderFactory;

  // Decoders should only be instantiated via DecoderFactory.
  explicit nsWEBPDecoder(RasterImage* aImage);

  enum class State
  {
    WEBP_DATA,
    FINISHED_WEBP_DATA
  };

  LexerTransition<State> ReadWEBPData(const char* aData, size_t aLength);
  LexerTransition<State> FinishedWEBPData();

  StreamingLexer<State> mLexer;

  WebPIDecoder *mDecoder;
  uint8_t *mData;          // Pointer to WebP-decoded data.
  int mPreviousLastLine;   // Last image scan-line read so far.

};

} // namespace image
} // namespace mozilla

#endif // nsWEBPDecoder_h__
