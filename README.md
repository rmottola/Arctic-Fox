# Arctic Fox Web Browser

Arctic Fox started as a forked and rebranded Pale Moon 27.9.4 and retains its _classic_ interface. Many fixes and enhancements have been imported from Firefox and TenFourFox.

Arctic Fox aims to be a desktop oriented browser with phone support removed, or no longer updated in the tree. Android has been aaxed, iOS is lingering.

The goal is to implement specific security updates and bug fixes to keep this browser as up to date as possible for aging systems. Examples would be Mac OSX 10.6-10.8, PowerPC's running Linux, Windows XP, etc.

Arctic Fox will build for Mac OS X 10.6 and up, Windows XP, i386/x86_64/PowerPC-BE, MIPS-el, ARM  on Linux, and more than likely any other Unix/BSD varient.
Ideally, we'd like to get it working on PowerPC 10.5 as well.
An older *very unofficial* 27.9.15 build can be found here: [Arctic Fox for 10.4/10.5](https://forums.macrumors.com/threads/so-this-finally-happened-sort-of.2172031/)

Compared to PaleMoon 27 some major changes:
* ARM support has been reinstantiated, including JIT!
* MIPS support reinstantiated, too (less tested and only on Little Endian)
* WebRTC has been reinstantiated

## Build tips

With enough swap, 1.2GB of RAM are the absolute minimum tested, 1.5GB is acceptable, 2GB is comfortable, 4GB is recommended. For some tricks, read below.

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


* To Build On FreeBSD
    * Up to 12.1 compilation fine with Clang and GCC.
    * On 12.2 clang fails to build, GCC completes, but binary unstable and fails to package

* To Build on OpenBSD
    * latest clang tried (8.0) needs -O0 or it will generate a crashing binary (x86-64 only)
    * x86 does not hang on sputnik javascript tests while on all other systems it does!
    * add LDFLAGS="-Wl,-z,wxneeded" to relax memory checks
    * add TAR=gtar

* To Build on NetBSD
    * every gcc version tested worked, system one being OK up to gcc8

If you are under memory pressure, try:
* use -g0 in your optimization flags (removes debug information, greatly reducing file sizes)

## What has been removed compared to FireFox?
* translation support through translations services
* social panel
* Android support
* metro support
* EME

## Resources

 * [Mozilla Source Code Directory Structure and links to project pages](https://developer.mozilla.org/en/Mozilla_Source_Code_Directory_Structure)
 * [Build Arctic Fox](https://github.com/rmottola/Arctic-Fox/wiki/Build-Instructions)
 
 ## Downloads and Add-ons
  See the *WIKI* tab for prebuilt binary download links.
  
 ## Thanks to...
  * The Pale Moon team for making a great browser to base this off of.
  * The TenFourFox team. We borrow or backport a lot of their stuff, their work is (has been) amazing.
