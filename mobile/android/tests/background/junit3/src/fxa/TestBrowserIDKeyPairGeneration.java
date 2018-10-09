/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.fxa;

import java.security.GeneralSecurityException;

import org.mozilla.goanna.background.helpers.AndroidSyncTestCase;
import org.mozilla.goanna.browserid.BrowserIDKeyPair;
import org.mozilla.goanna.browserid.DSACryptoImplementation;
import org.mozilla.goanna.browserid.JSONWebTokenUtils;
import org.mozilla.goanna.browserid.RSACryptoImplementation;
import org.mozilla.goanna.browserid.SigningPrivateKey;
import org.mozilla.goanna.browserid.VerifyingPublicKey;
import org.mozilla.goanna.sync.ExtendedJSONObject;
import org.mozilla.goanna.sync.Utils;

public class TestBrowserIDKeyPairGeneration extends AndroidSyncTestCase {
  public void doTestEncodeDecode(BrowserIDKeyPair keyPair) throws Exception {
    SigningPrivateKey privateKey = keyPair.getPrivate();
    VerifyingPublicKey publicKey = keyPair.getPublic();

    ExtendedJSONObject o = new ExtendedJSONObject();
    o.put("key", Utils.generateGuid());

    String token = JSONWebTokenUtils.encode(o.toJSONString(), privateKey);
    assertNotNull(token);

    String payload = JSONWebTokenUtils.decode(token, publicKey);
    assertEquals(o.toJSONString(), payload);

    try {
      JSONWebTokenUtils.decode(token + "x", publicKey);
      fail("Expected exception.");
    } catch (GeneralSecurityException e) {
      // Do nothing.
    }
  }

  public void testEncodeDecodeSuccessRSA() throws Exception {
    doTestEncodeDecode(RSACryptoImplementation.generateKeyPair(1024));
    doTestEncodeDecode(RSACryptoImplementation.generateKeyPair(2048));
  }

  public void testEncodeDecodeSuccessDSA() throws Exception {
    doTestEncodeDecode(DSACryptoImplementation.generateKeyPair(512));
    doTestEncodeDecode(DSACryptoImplementation.generateKeyPair(1024));
  }
}
