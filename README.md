# Arctic Fox Web Browser

This is a rebranded Pale Moon 27.9.4. It should be built with the 10.7 SDK to function properly on Mac OS X 10.6 Snow Leopard.
The goal here is to implement specific security updates and bug fixes to keep this snow leopard supported browser as up to date as possible.

It will also build for windows 7 and up, x86/x64 and powerpc linux (more than likely any other unix/bsd varient as well).

To build on MacOSX:
Requires 10.7 Lion minimum to build. Will fail on 10.6 due to missing frameworks in the 10.6 SDK. 
Install xcode, command line tools and macports. 
Install these via macports: 
sudo port -v install autoconf213 python27 libidl ccache yasm clang-3.7 (clang-3.7 is the minimum version known to work). 
Extract source archive somewhere convenient. 
Add a sane .mozconfig (i've included a working one). 
From the source directory type: ./mach build 
If it builds (takes about 1 hour) test it with: ./mach run 
Now package it: ./mach package 
The built package will be in /obj_x86_blah_blah/dist 

I did all my initial changes locally before creating this repository, but here is a short list of some changes that might not show here.
Full Rebranding / Fixed google captchas & craiglist reply button not working / Partially fixed the default OSX theme missing private window caption buttons (add-on themes now seem to display them properly however) / Updated certs from FF60ESR / Enabled canvas anti-fingerprinting and enabled private browsing by default / Changed feedback and release notes urls / tweaked some about:config prefs

## Resources

 * [Mozilla Source Code Directory Structure and links to project pages](https://developer.mozilla.org/en/Mozilla_Source_Code_Directory_Structure)
 * [Build Arctic Fox for Windows](https://forum.palemoon.org/viewtopic.php?f=19&t=13556)
 * [Build Arctic Fox for Linux](https://developer.palemoon.org/Developer_Guide:Build_Instructions/Pale_Moon/Linux)
 
 ## Downloads and Add-ons
  See the WIKI tab for download links.

