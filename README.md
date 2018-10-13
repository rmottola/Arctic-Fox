# Arctic Fox Web Browser

This is a rebranded Pale Moon 27.9.4. It should be built with the 10.7 SDK to function properly on Mac OS X 10.6 Snow Leopard.
The goal here is to implement specific security updates and bug fixes to keep this browser as up to date as possible for aging systems. Examples would be Mac OSX 10.6-10.8, PowerPC's running Linux, Win7, etc.

Arctic Fox will build for Windows 7 and up, x86/x64/PowerPC Linux, and more than likely any other unix/bsd varient as well.

* To build on MacOSX:
* Requires 10.7 Lion and 10.7 SDK minimum to build. Will fail on 10.6 due to missing frameworks in the 10.6 SDK. 
* It will also build on 10.6 IF you use the 10.7 SDK.
* Install xcode, command line tools and macports. 
* Install these via macports: 
* sudo port -v install autoconf213 python27 libidl ccache yasm clang-3.7 (clang-3.7 is the minimum known to work). 
* Extract source archive somewhere convenient. 
* Add a sane .mozconfig (i've included a working one for mac). 
* From the source directory type: ./mach build 
* If it builds (takes about 1 hour on a core2duo) test it with: ./mach run 
* Now package it: ./mach package 
* The built package will be in /obj_x86_blah_blah/dist 

## Resources

 * [Mozilla Source Code Directory Structure and links to project pages](https://developer.mozilla.org/en/Mozilla_Source_Code_Directory_Structure)
 * [Build Arctic Fox for Windows](https://forum.palemoon.org/viewtopic.php?f=19&t=13556)
 * [Build Arctic Fox for Linux](https://developer.palemoon.org/Developer_Guide:Build_Instructions/Pale_Moon/Linux)
 
 ## Downloads and Add-ons
  See the WIKI tab for download links.

