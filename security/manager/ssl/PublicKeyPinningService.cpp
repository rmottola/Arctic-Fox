/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PublicKeyPinningService.h"

#include "cert.h"
#include "mozilla/Base64.h"
#include "mozilla/Telemetry.h"
#include "nsISiteSecurityService.h"
#include "nssb64.h"
#include "nsServiceManagerUtils.h"
#include "nsSiteSecurityService.h"
#include "nsString.h"
#include "nsTArray.h"
#include "pkix/pkixtypes.h"
#include "mozilla/Logging.h"
#include "RootCertificateTelemetryUtils.h"
#include "ScopedNSSTypes.h"
#include "seccomon.h"
#include "sechash.h"

using namespace mozilla;
using namespace mozilla::pkix;
using namespace mozilla::psm;

PRLogModuleInfo* gPublicKeyPinningLog =
  PR_NewLogModule("PublicKeyPinningService");

/**
 Computes in the location specified by base64Out the SHA256 digest
 of the DER Encoded subject Public Key Info for the given cert
*/
static nsresult
GetBase64HashSPKI(const CERTCertificate* cert, SECOidTag hashType,
                  nsACString& hashSPKIDigest)
{
  hashSPKIDigest.Truncate();
  Digest digest;
  nsresult rv = digest.DigestBuf(hashType, cert->derPublicKey.data,
                                 cert->derPublicKey.len);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return Base64Encode(nsDependentCSubstring(
                        reinterpret_cast<const char*>(digest.get().data),
                        digest.get().len),
                      hashSPKIDigest);
}

/*
 * Returns true if a given cert matches any hashType fingerprints from the
 * given pinset or the dynamicFingeprints array, false otherwise.
 */
static nsresult
EvalCertWithHashType(const CERTCertificate* cert, SECOidTag hashType,
                     const nsTArray<nsCString>* dynamicFingerprints,
             /*out*/ bool& certMatchesPinset)
{
  certMatchesPinset = false;
  if (!dynamicFingerprints) {
    PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
           ("pkpin: No hashes found for hash type: %d\n", hashType));
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString base64Out;
  nsresult rv = GetBase64HashSPKI(cert, hashType, base64Out);
  if (NS_FAILED(rv)) {
    PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
           ("pkpin: GetBase64HashSPKI failed!\n"));
    return rv;
  }

  for (size_t i = 0; i < dynamicFingerprints->Length(); i++) {
    if (base64Out.Equals((*dynamicFingerprints)[i])) {
      PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
             ("pkpin: found pin base_64 ='%s'\n", base64Out.get()));
      certMatchesPinset = true;
      return NS_OK;
    }
  }
  
  return NS_OK;
}

/*
 * Returns true if a given chain matches any hashType fingerprints from the
 * given pinset or the dynamicFingerprints array, false otherwise.
 */
static nsresult
EvalChainWithHashType(const CERTCertList* certList, SECOidTag hashType,
                      const nsTArray<nsCString>* dynamicFingerprints,
              /*out*/ bool& certListIntersectsPinset)
{
  certListIntersectsPinset = false;
  CERTCertificate* currentCert;

  if (!dynamicFingerprints) {
    return NS_OK;
  }

  CERTCertListNode* node;
  for (node = CERT_LIST_HEAD(certList); !CERT_LIST_END(node, certList);
       node = CERT_LIST_NEXT(node)) {
    currentCert = node->cert;
    PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
           ("pkpin: certArray subject: '%s'\n", currentCert->subjectName));
    PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
           ("pkpin: certArray issuer: '%s'\n", currentCert->issuerName));
    nsresult rv = EvalCertWithHashType(currentCert, hashType,
                                       dynamicFingerprints,
                                       certListIntersectsPinset);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (certListIntersectsPinset) {
      return NS_OK;
    }
  }
  PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG, ("pkpin: no matches found\n"));
  return NS_OK;
}

nsresult
PublicKeyPinningService::ChainMatchesPinset(const CERTCertList* certList,
                                            const nsTArray<nsCString>& aSHA256keys,
                                    /*out*/ bool& chainMatchesPinset)
{
  return EvalChainWithHashType(certList, SEC_OID_SHA256, &aSHA256keys,
                               chainMatchesPinset);
}

// Returns via the output parameter the pinning information that is
// valid for the given host at the given time.
static nsresult
FindPinningInformation(const char* hostname, mozilla::pkix::Time time,
               /*out*/ nsTArray<nsCString>& dynamicFingerprints)
{
  if (!hostname || hostname[0] == 0) {
    return NS_ERROR_INVALID_ARG;
  }
  dynamicFingerprints.Clear();
  nsCOMPtr<nsISiteSecurityService> sssService =
    do_GetService(NS_SSSERVICE_CONTRACTID);
  if (!sssService) {
    return NS_ERROR_FAILURE;
  }
  SiteHPKPState dynamicEntry;
  char *evalHost = const_cast<char*>(hostname);
  char *evalPart;
  // Notice how the (xx = strchr) prevents pins for unqualified domain names.
  while (evalPart = strchr(evalHost, '.')) {
    PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
           ("pkpin: Querying pinsets for host: '%s'\n", evalHost));
    // Look up dynamic pins
    nsresult rv;
    bool found;
    bool includeSubdomains;
    nsTArray<nsCString> pinArray;
    rv = sssService->GetKeyPinsForHostname(evalHost, time, pinArray,
                                           &includeSubdomains, &found);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (found && (evalHost == hostname || includeSubdomains)) {
      PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
             ("pkpin: Found dyn match for host: '%s'\n", evalHost));
      dynamicFingerprints = pinArray;
      return NS_OK;
    } else {
      PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
             ("pkpin: Didn't find pinset for host: '%s'\n", evalHost));
    }
    // Add one for '.'
    evalHost = evalPart + 1;
  }
  return NS_OK;
}

// Returns true via the output parameter if the given certificate list meets
// pinning requirements for the given host at the given time. It must be the
// case that either there is an intersection between the set of hashes of
// subject public key info data in the list and the most relevant non-expired
// pinset for the host or there is no pinning information for the host.
static nsresult
CheckPinsForHostname(const CERTCertList* certList, const char* hostname,
                     bool enforceTestMode, mozilla::pkix::Time time,
             /*out*/ bool& chainHasValidPins)
{
  chainHasValidPins = false;
  if (!certList) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!hostname || hostname[0] == 0) {
    return NS_ERROR_INVALID_ARG;
  }

  nsTArray<nsCString> dynamicFingerprints;
  nsresult rv = FindPinningInformation(hostname, time, dynamicFingerprints);
  // If we have no pinning information, the certificate chain trivially
  // validates with respect to pinning.
  if (dynamicFingerprints.Length() == 0) {
    chainHasValidPins = true;
    return NS_OK;
  }
  return EvalChainWithHashType(certList, SEC_OID_SHA256,
                               &dynamicFingerprints, chainHasValidPins);
}

/**
 * Extract all the DNS names for a host (including CN) and evaluate the
 * certifiate pins against all of them (Currently is an OR so we stop
 * evaluating at the first OK pin).
 */
static nsresult
CheckChainAgainstAllNames(const CERTCertList* certList, bool enforceTestMode,
                          mozilla::pkix::Time time,
                  /*out*/ bool& chainHasValidPins)
{
  chainHasValidPins = false;
  PR_LOG(gPublicKeyPinningLog, PR_LOG_DEBUG,
         ("pkpin: top of checkChainAgainstAllNames"));
  CERTCertListNode* node = CERT_LIST_HEAD(certList);
  if (!node) {
    return NS_ERROR_INVALID_ARG;
  }
  CERTCertificate* cert = node->cert;
  if (!cert) {
    return NS_ERROR_INVALID_ARG;
  }

  ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return NS_ERROR_FAILURE;
  }

  CERTGeneralName* nameList;
  CERTGeneralName* currentName;
  nameList = CERT_GetConstrainedCertificateNames(cert, arena.get(), PR_TRUE);
  if (!nameList) {
    return NS_ERROR_FAILURE;
  }

  currentName = nameList;
  do {
    if (currentName->type == certDNSName
        && currentName->name.other.data[0] != 0) {
      // no need to cleaup, as the arena cleanup will do
      char *hostName = (char *)PORT_ArenaAlloc(arena.get(),
                                               currentName->name.other.len + 1);
      if (!hostName) {
        break;
      }
      // We use a temporary buffer as the hostname as returned might not be
      // null terminated.
      hostName[currentName->name.other.len] = 0;
      memcpy(hostName, currentName->name.other.data,
             currentName->name.other.len);
      if (!hostName[0]) {
        // cannot call CheckPinsForHostname on empty or null hostname
        break;
      }
      nsAutoCString canonicalizedHostname(
        PublicKeyPinningService::CanonicalizeHostname(hostName));
      nsresult rv = CheckPinsForHostname(certList, canonicalizedHostname.get(),
                                         enforceTestMode, time,
                                         chainHasValidPins);
      if (NS_FAILED(rv)) {
        return rv;
      }
      if (chainHasValidPins) {
        return NS_OK;
      }
    }
    currentName = CERT_GetNextGeneralName(currentName);
  } while (currentName != nameList);

  return NS_OK;
}

nsresult
PublicKeyPinningService::ChainHasValidPins(const CERTCertList* certList,
                                           const char* hostname,
                                           mozilla::pkix::Time time,
                                           bool enforceTestMode,
                                   /*out*/ bool& chainHasValidPins)
{
  chainHasValidPins = false;
  if (!certList) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!hostname || hostname[0] == 0) {
    return CheckChainAgainstAllNames(certList, enforceTestMode, time,
                                     chainHasValidPins);
  }
  nsAutoCString canonicalizedHostname(CanonicalizeHostname(hostname));
  return CheckPinsForHostname(certList, canonicalizedHostname.get(),
                              enforceTestMode, time, chainHasValidPins);
}

nsresult
PublicKeyPinningService::HostHasPins(const char* hostname,
                                     mozilla::pkix::Time time,
                                     bool enforceTestMode,
                                     /*out*/ bool& hostHasPins)
{
  hostHasPins = false;
  nsAutoCString canonicalizedHostname(CanonicalizeHostname(hostname));
  nsTArray<nsCString> dynamicFingerprints;
  nsresult rv = FindPinningInformation(canonicalizedHostname.get(), time,
                                       dynamicFingerprints);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (dynamicFingerprints.Length() > 0) {
    hostHasPins = true;
  }
  return NS_OK;
}

nsAutoCString
PublicKeyPinningService::CanonicalizeHostname(const char* hostname)
{
  nsAutoCString canonicalizedHostname(hostname);
  ToLowerCase(canonicalizedHostname);
  while (canonicalizedHostname.Length() > 0 &&
         canonicalizedHostname.Last() == '.') {
    canonicalizedHostname.Truncate(canonicalizedHostname.Length() - 1);
  }
  return canonicalizedHostname;
}
