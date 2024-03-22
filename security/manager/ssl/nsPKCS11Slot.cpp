/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPKCS11Slot.h"

#include "mozilla/Logging.h"
#include "mozilla/Telemetry.h"
#include "nsCOMPtr.h"
#include "nsIMutableArray.h"
#include "nsPK11TokenDB.h"
#include "secmod.h"

using mozilla::LogLevel;

extern mozilla::LazyLogModule gPIPNSSLog;

NS_IMPL_ISUPPORTS(nsPKCS11Slot, nsIPKCS11Slot)

nsPKCS11Slot::nsPKCS11Slot(PK11SlotInfo *slot)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return;

  PK11_ReferenceSlot(slot);
  mSlot = slot;
  mSeries = PK11_GetSlotSeries(slot);
  refreshSlotInfo();
}

void
nsPKCS11Slot::refreshSlotInfo()
{
  CK_SLOT_INFO slot_info;
  if (PK11_GetSlotInfo(mSlot, &slot_info) == SECSuccess) {
    // Set the Description field
    const char *ccDesc = (const char*)slot_info.slotDescription;
    const nsACString &cDesc = Substring(
      ccDesc, 
      ccDesc+PL_strnlen(ccDesc, sizeof(slot_info.slotDescription)));
    mSlotDesc = NS_ConvertUTF8toUTF16(cDesc);
    mSlotDesc.Trim(" ", false, true);
    // Set the Manufacturer field
    const char *ccManID = (const char*)slot_info.manufacturerID;
    const nsACString &cManID = Substring(
      ccManID, 
      ccManID+PL_strnlen(ccManID, sizeof(slot_info.manufacturerID)));
    mSlotManID = NS_ConvertUTF8toUTF16(cManID);
    mSlotManID.Trim(" ", false, true);
    // Set the Hardware Version field
    mSlotHWVersion = EmptyString();
    mSlotHWVersion.AppendInt(slot_info.hardwareVersion.major);
    mSlotHWVersion.Append('.');
    mSlotHWVersion.AppendInt(slot_info.hardwareVersion.minor);
    // Set the Firmware Version field
    mSlotFWVersion = EmptyString();
    mSlotFWVersion.AppendInt(slot_info.firmwareVersion.major);
    mSlotFWVersion.Append('.');
    mSlotFWVersion.AppendInt(slot_info.firmwareVersion.minor);
  }

}

nsPKCS11Slot::~nsPKCS11Slot()
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown()) {
    return;
  }
  destructorSafeDestroyNSSReference();
  shutdown(calledFromObject);
}

void nsPKCS11Slot::virtualDestroyNSSReference()
{
  destructorSafeDestroyNSSReference();
}

void nsPKCS11Slot::destructorSafeDestroyNSSReference()
{
  if (mSlot) {
    PK11_FreeSlot(mSlot);
    mSlot = nullptr;
  }
}

NS_IMETHODIMP 
nsPKCS11Slot::GetName(char16_t **aName)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  char *csn = PK11_GetSlotName(mSlot);
  if (*csn) {
    *aName = ToNewUnicode(NS_ConvertUTF8toUTF16(csn));
  } else if (PK11_HasRootCerts(mSlot)) {
    // This is a workaround to an Root Module bug - the root certs module has
    // no slot name.  Not bothering to localize, because this is a workaround
    // and for now all the slot names returned by NSS are char * anyway.
    *aName = ToNewUnicode(NS_LITERAL_STRING("Root Certificates"));
  } else {
    // same as above, this is a catch-all
    *aName = ToNewUnicode(NS_LITERAL_STRING("Unnamed Slot"));
  }
  if (!*aName) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetDesc(char16_t **aDesc)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  if (mSeries != PK11_GetSlotSeries(mSlot)) {
    refreshSlotInfo();
  }

  *aDesc = ToNewUnicode(mSlotDesc);
  if (!*aDesc) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetManID(char16_t **aManID)
{
  if (mSeries != PK11_GetSlotSeries(mSlot)) {
    refreshSlotInfo();
  }
  *aManID = ToNewUnicode(mSlotManID);
  if (!*aManID) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetHWVersion(char16_t **aHWVersion)
{
  if (mSeries != PK11_GetSlotSeries(mSlot)) {
    refreshSlotInfo();
  }
  *aHWVersion = ToNewUnicode(mSlotHWVersion);
  if (!*aHWVersion) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetFWVersion(char16_t **aFWVersion)
{
  if (mSeries != PK11_GetSlotSeries(mSlot)) {
    refreshSlotInfo();
  }
  *aFWVersion = ToNewUnicode(mSlotFWVersion);
  if (!*aFWVersion) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetToken(nsIPK11Token **_retval)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  nsCOMPtr<nsIPK11Token> token = new nsPK11Token(mSlot);
  token.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Slot::GetTokenName(char16_t **aName)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  if (!PK11_IsPresent(mSlot)) {
    *aName = nullptr;
    return NS_OK;
  }

  if (mSeries != PK11_GetSlotSeries(mSlot)) {
    refreshSlotInfo();
  }


  *aName = ToNewUnicode(NS_ConvertUTF8toUTF16(PK11_GetTokenName(mSlot)));
  if (!*aName) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

NS_IMETHODIMP
nsPKCS11Slot::GetStatus(uint32_t *_retval)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  if (PK11_IsDisabled(mSlot))
    *_retval = SLOT_DISABLED;
  else if (!PK11_IsPresent(mSlot))
    *_retval = SLOT_NOT_PRESENT;
  else if (PK11_NeedLogin(mSlot) && PK11_NeedUserInit(mSlot))
    *_retval = SLOT_UNINITIALIZED;
  else if (PK11_NeedLogin(mSlot) && !PK11_IsLoggedIn(mSlot, nullptr))
    *_retval = SLOT_NOT_LOGGED_IN;
  else if (PK11_NeedLogin(mSlot))
    *_retval = SLOT_LOGGED_IN;
  else
    *_retval = SLOT_READY;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsPKCS11Module, nsIPKCS11Module)

nsPKCS11Module::nsPKCS11Module(SECMODModule *module)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return;

  SECMOD_ReferenceModule(module);
  mModule = module;
}

nsPKCS11Module::~nsPKCS11Module()
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown()) {
    return;
  }
  destructorSafeDestroyNSSReference();
  shutdown(calledFromObject);
}

void nsPKCS11Module::virtualDestroyNSSReference()
{
  destructorSafeDestroyNSSReference();
}

void nsPKCS11Module::destructorSafeDestroyNSSReference()
{
  if (mModule) {
    SECMOD_DestroyModule(mModule);
    mModule = nullptr;
  }
}

NS_IMETHODIMP 
nsPKCS11Module::GetName(char16_t **aName)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  *aName = ToNewUnicode(NS_ConvertUTF8toUTF16(mModule->commonName));
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Module::GetLibName(char16_t **aName)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  if ( mModule->dllName ) {
    *aName = ToNewUnicode(NS_ConvertUTF8toUTF16(mModule->dllName));
  } else {
    *aName = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11Module::FindSlotByName(const char16_t *aName,
                               nsIPKCS11Slot **_retval)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown())
    return NS_ERROR_NOT_AVAILABLE;

  char *asciiname = ToNewUTF8String(nsDependentString(aName));
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("Getting \"%s\"\n", asciiname));
  PK11SlotInfo *slotinfo = nullptr;
  PK11SlotList *slotList = PK11_FindSlotsByNames(mModule->dllName, 
        asciiname /* slotName */, nullptr /* token Name */, false);
  if (!slotList) {
    /* name must be the token name */
    slotList = PK11_FindSlotsByNames(mModule->dllName, 
        nullptr /*slot Name */, asciiname /* token Name */, false);
  }
  if (slotList) {
    /* should only be one */
    if (slotList->head && slotList->head->slot) {
      slotinfo =  PK11_ReferenceSlot(slotList->head->slot);
    }
    PK11_FreeSlotList(slotList);
  }
  if (!slotinfo) {
    // workaround - the builtin module has no name
    if (!asciiname) {
      return NS_ERROR_FAILURE;
    } else if (nsCRT::strcmp(asciiname, "Root Certificates") == 0) {
      slotinfo = PK11_ReferenceSlot(mModule->slots[0]);
    } else {
      // give up
      free(asciiname);
      return NS_ERROR_FAILURE;
    }
  } 
  free(asciiname);
  nsCOMPtr<nsIPKCS11Slot> slot = new nsPKCS11Slot(slotinfo);
  PK11_FreeSlot(slotinfo);
  slot.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsPKCS11Module::ListSlots(nsISimpleEnumerator** _retval)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIMutableArray> array = do_CreateInstance(NS_ARRAY_CONTRACTID);
  if (!array) {
    return NS_ERROR_FAILURE;
  }

  /* applications which allow new slot creation (which Firefox now does
   * since it uses the WaitForSlotEvent call) need to hold the
   * ModuleList Read lock to prevent the slot array from changing out
   * from under it. */
  AutoSECMODListReadLock lock;
  for (int i = 0; i < mModule->slotCount; i++) {
    if (mModule->slots[i]) {
      nsCOMPtr<nsIPKCS11Slot> slot = new nsPKCS11Slot(mModule->slots[i]);
      nsresult rv = array->AppendElement(slot, false);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  return array->Enumerate(_retval);
}

NS_IMPL_ISUPPORTS(nsPKCS11ModuleDB, nsIPKCS11ModuleDB, nsICryptoFIPSInfo)

nsPKCS11ModuleDB::nsPKCS11ModuleDB()
{
}

nsPKCS11ModuleDB::~nsPKCS11ModuleDB()
{
}

NS_IMETHODIMP 
nsPKCS11ModuleDB::GetInternal(nsIPKCS11Module **_retval)
{
  nsNSSShutDownPreventionLock locker;
  SECMODModule *nssMod = 
    SECMOD_CreateModule(nullptr, SECMOD_INT_NAME, nullptr, SECMOD_INT_FLAGS);
  nsCOMPtr<nsIPKCS11Module> module = new nsPKCS11Module(nssMod);
  SECMOD_DestroyModule(nssMod);
  module.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11ModuleDB::GetInternalFIPS(nsIPKCS11Module **_retval)
{
  nsNSSShutDownPreventionLock locker;
  SECMODModule *nssMod = 
    SECMOD_CreateModule(nullptr, SECMOD_FIPS_NAME, nullptr, SECMOD_FIPS_FLAGS);
  nsCOMPtr<nsIPKCS11Module> module = new nsPKCS11Module(nssMod);
  SECMOD_DestroyModule(nssMod);
  module.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP 
nsPKCS11ModuleDB::FindModuleByName(const char16_t *aName,
                                   nsIPKCS11Module **_retval)
{
  nsNSSShutDownPreventionLock locker;
  NS_ConvertUTF16toUTF8 aUtf8Name(aName);
  SECMODModule *mod =
    SECMOD_FindModule(const_cast<char *>(aUtf8Name.get()));
  if (!mod)
    return NS_ERROR_FAILURE;
  nsCOMPtr<nsIPKCS11Module> module = new nsPKCS11Module(mod);
  SECMOD_DestroyModule(mod);
  module.forget(_retval);
  return NS_OK;
}

/* This is essentially the same as nsIPK11Token::findTokenByName, except
 * that it returns an nsIPKCS11Slot, which may be desired.
 */
NS_IMETHODIMP 
nsPKCS11ModuleDB::FindSlotByName(const char16_t *aName,
                                 nsIPKCS11Slot **_retval)
{
  nsNSSShutDownPreventionLock locker;
  NS_ConvertUTF16toUTF8 aUtf8Name(aName);
  PK11SlotInfo *slotinfo =
   PK11_FindSlotByName(const_cast<char*>(aUtf8Name.get()));
  if (!slotinfo)
    return NS_ERROR_FAILURE;
  nsCOMPtr<nsIPKCS11Slot> slot = new nsPKCS11Slot(slotinfo);
  PK11_FreeSlot(slotinfo);
  slot.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsPKCS11ModuleDB::ListModules(nsISimpleEnumerator** _retval)
{
  nsNSSShutDownPreventionLock locker;
  nsCOMPtr<nsIMutableArray> array = do_CreateInstance(NS_ARRAY_CONTRACTID);
  if (!array) {
    return NS_ERROR_FAILURE;
  }

  /* lock down the list for reading */
  AutoSECMODListReadLock lock;
  for (SECMODModuleList* list = SECMOD_GetDefaultModuleList(); list;
       list = list->next) {
    nsCOMPtr<nsIPKCS11Module> module = new nsPKCS11Module(list->module);
    nsresult rv = array->AppendElement(module, false);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  /* Get the modules in the database that didn't load */
  for (SECMODModuleList* list = SECMOD_GetDeadModuleList(); list;
       list = list->next) {
    nsCOMPtr<nsIPKCS11Module> module = new nsPKCS11Module(list->module);
    nsresult rv = array->AppendElement(module, false);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return array->Enumerate(_retval);
}

NS_IMETHODIMP nsPKCS11ModuleDB::GetCanToggleFIPS(bool *aCanToggleFIPS)
{
  nsNSSShutDownPreventionLock locker;
  *aCanToggleFIPS = SECMOD_CanDeleteInternalModule();
  return NS_OK;
}


NS_IMETHODIMP nsPKCS11ModuleDB::ToggleFIPSMode()
{
  nsNSSShutDownPreventionLock locker;
  // The way to toggle FIPS mode in NSS is extremely obscure.
  // Basically, we delete the internal module, and voila it
  // gets replaced with the opposite module, ie if it was 
  // FIPS before, then it becomes non-FIPS next.
  SECMODModule *internal;

  // This function returns us a pointer to a local copy of 
  // the internal module stashed in NSS.  We don't want to
  // delete it since it will cause much pain in NSS.
  internal = SECMOD_GetInternalModule();
  if (!internal)
    return NS_ERROR_FAILURE;

  SECStatus srv = SECMOD_DeleteInternalModule(internal->commonName);
  if (srv != SECSuccess)
    return NS_ERROR_FAILURE;

  if (PK11_IsFIPS()) {
    Telemetry::Accumulate(Telemetry::FIPS_ENABLED, true);
  }

  return NS_OK;
}

NS_IMETHODIMP nsPKCS11ModuleDB::GetIsFIPSEnabled(bool *aIsFIPSEnabled)
{
  nsNSSShutDownPreventionLock locker;
  *aIsFIPSEnabled = PK11_IsFIPS();
  return NS_OK;
}

NS_IMETHODIMP nsPKCS11ModuleDB::GetIsFIPSModeActive(bool *aIsFIPSModeActive)
{
  return GetIsFIPSEnabled(aIsFIPSModeActive);
}
