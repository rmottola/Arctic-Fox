// -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

"use strict";

function build_cert_chain(certNames) {
  let certList = Cc["@mozilla.org/security/x509certlist;1"]
                   .createInstance(Ci.nsIX509CertList);
  certNames.forEach(function(certName) {
    let cert = constructCertFromFile("tlsserver/" + certName + ".der");
    certList.addCert(cert);
  });
  return certList;
}

function test_cert_equals() {
  let certA = constructCertFromFile("tlsserver/default-ee.der");
  let certB = constructCertFromFile("tlsserver/default-ee.der");
  let certC = constructCertFromFile("tlsserver/expired-ee.der");

  do_check_false(certA == certB);
  do_check_true(certA.equals(certB));
  do_check_false(certA.equals(certC));
}

function test_bad_cert_list_serialization() {
  // Normally the serialization of an nsIX509CertList consists of some header
  // junk (IIDs and whatnot), 4 bytes representing how many nsIX509Cert follow,
  // and then the serialization of each nsIX509Cert. This serialization consists
  // of the header junk for an nsIX509CertList with 1 "nsIX509Cert", but then
  // instead of an nsIX509Cert, the subsequent bytes represent the serialization
  // of another nsIX509CertList (with 0 nsIX509Cert). This test ensures that
  // nsIX509CertList safely handles this unexpected input when deserializing.
  const badCertListSerialization =
    "lZ+xZWUXSH+rm9iRO+UxlwAAAAAAAAAAwAAAAAAAAEYAAAABlZ+xZWUXSH+rm9iRO+UxlwAAAAAA" +
    "AAAAwAAAAAAAAEYAAAAA";
  let serHelper = Cc["@mozilla.org/network/serialization-helper;1"]
                    .getService(Ci.nsISerializationHelper);
  throws(() => serHelper.deserializeObject(badCertListSerialization),
         /NS_ERROR_UNEXPECTED/,
         "deserializing a bogus nsIX509CertList should throw NS_ERROR_UNEXPECTED");
}


function test_cert_list_serialization() {
  let certList = build_cert_chain(['default-ee', 'expired-ee']);

  throws(() => certList.addCert(null), /NS_ERROR_ILLEGAL_VALUE/,
         "trying to add a null cert to an nsIX509CertList should throw");

  // Serialize the cert list to a string
  let serHelper = Cc["@mozilla.org/network/serialization-helper;1"]
                    .getService(Ci.nsISerializationHelper);
  certList.QueryInterface(Ci.nsISerializable);
  let serialized = serHelper.serializeToString(certList);

  // Deserialize from the string and compare to the original object
  let deserialized = serHelper.deserializeObject(serialized);
  deserialized.QueryInterface(Ci.nsIX509CertList);
  do_check_true(certList.equals(deserialized));
}

function test_security_info_serialization(securityInfo, expectedErrorCode) {
  // Serialize the securityInfo to a string
  let serHelper = Cc["@mozilla.org/network/serialization-helper;1"]
                    .getService(Ci.nsISerializationHelper);
  let serialized = serHelper.serializeToString(securityInfo);

  // Deserialize from the string and compare to the original object
  let deserialized = serHelper.deserializeObject(serialized);
  deserialized.QueryInterface(Ci.nsITransportSecurityInfo);
  do_check_eq(securityInfo.securityState, deserialized.securityState);
  do_check_eq(securityInfo.errorMessage, deserialized.errorMessage);
  do_check_eq(securityInfo.errorCode, expectedErrorCode);
  do_check_eq(deserialized.errorCode, expectedErrorCode);
}

function run_test() {
  do_get_profile();
  add_tls_server_setup("BadCertServer");

  // Test nsIX509Cert.equals
  add_test(function() {
    test_cert_equals();
    run_next_test();
  });

  add_test(function() {
    test_bad_cert_list_serialization();
    run_next_test();
  });

  // Test serialization of nsIX509CertList
  add_test(function() {
    test_cert_list_serialization();
    run_next_test();
  });

  // Test successful connection (failedCertChain should be null)
  add_connection_test(
    // re-use pinning certs (keeler)
    "good.include-subdomains.pinning.example.com", PRErrorCodeSuccess, null,
    function withSecurityInfo(aTransportSecurityInfo) {
      aTransportSecurityInfo.QueryInterface(Ci.nsITransportSecurityInfo);
      test_security_info_serialization(aTransportSecurityInfo, 0);
      do_check_eq(aTransportSecurityInfo.failedCertChain, null);
    }
  );

  // Test overrideable connection failure (failedCertChain should be non-null)
  add_connection_test(
    "expired.example.com",
    SEC_ERROR_EXPIRED_CERTIFICATE,
    null,
    function withSecurityInfo(securityInfo) {
      securityInfo.QueryInterface(Ci.nsITransportSecurityInfo);
      test_security_info_serialization(securityInfo, SEC_ERROR_EXPIRED_CERTIFICATE);
      do_check_neq(securityInfo.failedCertChain, null);
      let originalCertChain = build_cert_chain(["expired-ee", "test-ca"]);
      do_check_true(originalCertChain.equals(securityInfo.failedCertChain));
    }
  );

  // Test non-overrideable error (failedCertChain should be non-null)
  add_connection_test(
    "inadequatekeyusage.example.com",
    SEC_ERROR_INADEQUATE_KEY_USAGE,
    null,
    function withSecurityInfo(securityInfo) {
      securityInfo.QueryInterface(Ci.nsITransportSecurityInfo);
      test_security_info_serialization(securityInfo, SEC_ERROR_INADEQUATE_KEY_USAGE);
      do_check_neq(securityInfo.failedCertChain, null);
      let originalCertChain = build_cert_chain(["inadequatekeyusage-ee", "test-ca"]);
      do_check_true(originalCertChain.equals(securityInfo.failedCertChain));
    }
  );

  run_next_test();
}
