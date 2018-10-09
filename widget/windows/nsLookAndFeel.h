/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsLookAndFeel
#define __nsLookAndFeel

#include "nsXPLookAndFeel.h"
#include "nsIWindowsRegKey.h"

/*
 * Gesture System Metrics
 */
#ifndef SM_DIGITIZER
#define SM_DIGITIZER         94
#define TABLET_CONFIG_NONE   0x00000000
#define NID_INTEGRATED_TOUCH 0x00000001
#define NID_EXTERNAL_TOUCH   0x00000002
#define NID_INTEGRATED_PEN   0x00000004
#define NID_EXTERNAL_PEN     0x00000008
#define NID_MULTI_INPUT      0x00000040
#define NID_READY            0x00000080
#endif

class nsLookAndFeel: public nsXPLookAndFeel {
  static OperatingSystemVersion GetOperatingSystemVersion();
public:
  nsLookAndFeel();
  virtual ~nsLookAndFeel();

  virtual nsresult NativeGetColor(ColorID aID, nscolor &aResult);
  virtual nsresult GetIntImpl(IntID aID, int32_t &aResult);
  virtual nsresult GetFloatImpl(FloatID aID, float &aResult);
  virtual bool GetFontImpl(FontID aID, nsString& aFontName,
                           gfxFontStyle& aFontStyle,
                           float aDevPixPerCSSPixel);
  virtual char16_t GetPasswordCharacterImpl();
private:
  /**
   * Fetches the Windows accent color from the Windows settings if
   * the accent color is set to apply to the title bar, otherwise
   * returns an error code.
   */
  nsresult GetAccentColor(nscolor& aColor);

  /**
   * check if the accent color is dark enough to need white text
   **/
  bool AccentColorIsDark(nscolor aColor);
  
  /**
   * If the Windows accent color from the Windows settings is set
   * to apply to the title bar, this computes the color that should
   * be used for text that is to be written over a background that has
   * the accent color.  Otherwise, (if the accent color should not
   * apply to the title bar) this returns an error code.
   */
  nsresult GetAccentColorText(nscolor& aColor);
 
  nsCOMPtr<nsIWindowsRegKey> mDwmKey;
};

#endif
