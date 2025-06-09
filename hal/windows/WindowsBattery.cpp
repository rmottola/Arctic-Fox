/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Hal.h"
#include "HalImpl.h"
#include "nsITimer.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/battery/Constants.h"
#include "nsComponentManagerUtils.h"

#include <windows.h>
#include "mozilla/WindowsVersion.h"

using namespace mozilla::dom::battery;

namespace mozilla {
namespace hal_impl {

static nsCOMPtr<nsITimer> sUpdateTimer;

/* Power Event API is Vista or later */
static decltype(RegisterPowerSettingNotification)* sRegisterPowerSettingNotification = nullptr;
static decltype(UnregisterPowerSettingNotification)* sUnregisterPowerSettingNotification = nullptr;
static HPOWERNOTIFY sPowerHandle = nullptr;
static HPOWERNOTIFY sCapacityHandle = nullptr;
static HWND sHWnd = nullptr;

static void
UpdateHandler(nsITimer* aTimer, void* aClosure) {
  NS_ASSERTION(!IsVistaOrLater(),
               "We shouldn't call this function for Vista or later version!");

  static hal::BatteryInformation sLastInfo;
  hal::BatteryInformation currentInfo;

  hal_impl::GetCurrentBatteryInformation(&currentInfo);
  if (sLastInfo.level() != currentInfo.level() ||
      sLastInfo.charging() != currentInfo.charging() ||
      sLastInfo.remainingTime() != currentInfo.remainingTime()) {
    hal::NotifyBatteryChange(currentInfo);
    sLastInfo = currentInfo;
  }
}

static
LRESULT CALLBACK
BatteryWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg != WM_POWERBROADCAST || wParam != PBT_POWERSETTINGCHANGE) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  hal::BatteryInformation currentInfo;

  // Since we need update remainingTime, we cannot use LPARAM.
  hal_impl::GetCurrentBatteryInformation(&currentInfo);

  hal::NotifyBatteryChange(currentInfo);
  return TRUE;
}

void
EnableBatteryNotifications()
{
  if (IsVistaOrLater()) {
    // RegisterPowerSettingNotification is from Vista or later.
    // Use this API if available.
    HMODULE hUser32 = GetModuleHandleW(L"USER32.DLL");
    if (!sRegisterPowerSettingNotification)
      sRegisterPowerSettingNotification = (decltype(RegisterPowerSettingNotification)*)
        GetProcAddress(hUser32, "RegisterPowerSettingNotification");
    if (!sUnregisterPowerSettingNotification)
      sUnregisterPowerSettingNotification = (decltype(UnregisterPowerSettingNotification)*)
        GetProcAddress(hUser32, "UnregisterPowerSettingNotification");

    if (!sRegisterPowerSettingNotification ||
        !sUnregisterPowerSettingNotification) {
      NS_ASSERTION(false, "Canot find PowerSettingNotification functions.");
      return;
    }

    // Create custom window to watch battery event
    // If we can get Gecko's window handle, this is unnecessary.

    if (sHWnd == nullptr) {
      WNDCLASSW wc;
      HMODULE hSelf = GetModuleHandle(nullptr);

      if (!GetClassInfoW(hSelf, L"MozillaBatteryClass", &wc)) {
        ZeroMemory(&wc, sizeof(WNDCLASSW));
        wc.hInstance = hSelf;
        wc.lpfnWndProc = BatteryWindowProc;
        wc.lpszClassName = L"MozillaBatteryClass";
        RegisterClassW(&wc);
      }

      sHWnd = CreateWindowW(L"MozillaBatteryClass", L"Battery Watcher",
                            0, 0, 0, 0, 0,
                            nullptr, nullptr, hSelf, nullptr);
    }

    if (sHWnd == nullptr) {
      return;
    }

    sPowerHandle =
      sRegisterPowerSettingNotification(sHWnd,
                                        &GUID_ACDC_POWER_SOURCE,
                                        DEVICE_NOTIFY_WINDOW_HANDLE);
    sCapacityHandle =
      sRegisterPowerSettingNotification(sHWnd,
                                        &GUID_BATTERY_PERCENTAGE_REMAINING,
                                        DEVICE_NOTIFY_WINDOW_HANDLE);
  } else
  {
    // for Windows XP.  If we remove Windows XP support,
    // we should remove timer-based power notification
    sUpdateTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
    if (sUpdateTimer) {
      sUpdateTimer->InitWithFuncCallback(UpdateHandler,
                                         nullptr,
                                         Preferences::GetInt("dom.battery.timer",
                                                             30000 /* 30s */),
                                         nsITimer::TYPE_REPEATING_SLACK);
    } 
  }
}

void
DisableBatteryNotifications()
{
  if (IsVistaOrLater()) {
    if (sPowerHandle) {
      sUnregisterPowerSettingNotification(sPowerHandle);
      sPowerHandle = nullptr;
    }

    if (sCapacityHandle) {
      sUnregisterPowerSettingNotification(sCapacityHandle);
      sCapacityHandle = nullptr;
    }

    if (sHWnd) {
      DestroyWindow(sHWnd);
      sHWnd = nullptr;
    }
  } else
  {
    if (sUpdateTimer) {
      sUpdateTimer->Cancel();
      sUpdateTimer = nullptr;
    }
  }
}

void
GetCurrentBatteryInformation(hal::BatteryInformation* aBatteryInfo)
{
  SYSTEM_POWER_STATUS status;
  if (!GetSystemPowerStatus(&status)) {
    aBatteryInfo->level() = kDefaultLevel;
    aBatteryInfo->charging() = kDefaultCharging;
    aBatteryInfo->remainingTime() = kDefaultRemainingTime;
    return;
  }

  aBatteryInfo->level() =
    status.BatteryLifePercent == 255 ? kDefaultLevel
                                     : ((double)status.BatteryLifePercent) / 100.0;
  aBatteryInfo->charging() = (status.ACLineStatus != 0);
  if (status.ACLineStatus != 0) {
    if (aBatteryInfo->level() == 1.0) {
      // GetSystemPowerStatus API may returns -1 for BatteryFullLifeTime.
      // So, if battery is 100%, set kDefaultRemainingTime at force.
      aBatteryInfo->remainingTime() = kDefaultRemainingTime;
    } else {
      aBatteryInfo->remainingTime() =
        status.BatteryFullLifeTime == (DWORD)-1 ? kUnknownRemainingTime
                                                : status.BatteryFullLifeTime;
    }
  } else {
    aBatteryInfo->remainingTime() =
      status.BatteryLifeTime == (DWORD)-1 ? kUnknownRemainingTime
                                          : status.BatteryLifeTime;
  }
}

} // hal_impl
} // mozilla
