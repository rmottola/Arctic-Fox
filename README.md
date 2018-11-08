# Arctic Fox Web Browser

Arctic Fox is a forked and rebranded Pale Moon 27.9.4.
The goal here is to implement specific security updates and bug fixes to keep this browser as up to date as possible for aging systems. Examples would be Mac OSX 10.6-10.8, PowerPC's running Linux, etc.

Arctic Fox will build for Mac OS X 10.6 and up, i386/x86_x64/PowerPC Linux, and more than likely any other unix/bsd varient as well. Ideally, we'd like to get it working on 32-bit OS X and PPC 10.5 also.

* To build on MacOSX:
* Requires OS X 10.6 as a minimum build environment.
* Install xcode, command line tools and macports. 
* Install these via macports: 
* sudo port -v install autoconf213 python27 libidl ccache yasm clang-3.7 (clang-3.7 is the minimum known to work). 
* Extract source archive somewhere convenient. 
* Add a sane .mozconfig (i've included some samples). 
* From the source directory type: ./mach build 
* If it builds (takes about 1 hour on a core2duo) test it with: ./mach run 
* Now package it: ./mach package 
* The built package will be in /obj_blah_blah/dist 

## Resources

 * [Mozilla Source Code Directory Structure and links to project pages](https://developer.mozilla.org/en/Mozilla_Source_Code_Directory_Structure)
 * [Build Arctic Fox for Windows](https://forum.palemoon.org/viewtopic.php?f=19&t=13556)
 * [Build Arctic Fox for Linux](https://developer.palemoon.org/Developer_Guide:Build_Instructions/Pale_Moon/Linux)
 
 ## Downloads and Add-ons
  See the WIKI tab for prebuilt binary download links.

