/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrVkUtil_DEFINED
#define GrVkUtil_DEFINED

#include "GrColor.h"
#include "GrTypes.h"
#include "vk/GrVkInterface.h"

#include "vk/GrVkDefines.h"

// makes a Vk call on the interface
#define GR_VK_CALL(IFACE, X) (IFACE)->fFunctions.f##X;
// same as GR_VK_CALL but checks for success
#ifdef SK_DEBUG
#define GR_VK_CALL_ERRCHECK(IFACE, X) \
    VkResult SK_MACRO_APPEND_LINE(ret) = GR_VK_CALL(IFACE, X); \
    SkASSERT(VK_SUCCESS == SK_MACRO_APPEND_LINE(ret));
#else
#define GR_VK_CALL_ERRCHECK(IFACE, X)  (void) GR_VK_CALL(IFACE, X);
#endif

/**
 * Returns the vulkan texture format for the given GrPixelConfig
 */
bool GrPixelConfigToVkFormat(GrPixelConfig config, VkFormat* format);

/**
* Returns the GrPixelConfig for the given vulkan texture format
*/
bool GrVkFormatToPixelConfig(VkFormat format, GrPixelConfig* config);

bool GrSampleCountToVkSampleCount(uint32_t samples, VkSampleCountFlagBits* vkSamples);

#endif
