/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

if(!caligon) var caligon = {};

window.addEventListener("load", function buildS4E()
{
	window.removeEventListener("load", buildS4E, false);

	Components.utils.import("resource:///modules/statusbar/Status4Evar.jsm");

	caligon.status4evar = new Status4Evar(window, gBrowser, gNavToolbox);
	caligon.status4evar.setup();
}, false);

