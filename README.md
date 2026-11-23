## Disclaimer

  The project is currently a proof of concept with the goals
  laid out that will hopefully be implemented in due course.
  The hope is that the project can be completely open source
  but I need your help to ensure that.  Please read this
  entire file and then if you like the concepts then
  [Buying me a coffee](https://buymeacoffee.com/robertapengelly)
  or [Making a small donation through PayPal](https://www.paypal.me/robertapengelly92)
  will go a long way.

## License

  All source code is Public Domain.

## Obtain the source code

  git clone https://git.candlhat.org/chimaera.git

## Building

  BSD:
  
      Make sure you have gcc and gmake installed then run
      gmake -f Makefile.unix.
  
  Linux:
  
      Make sure you have gcc and make installed then run
      make -f Makefile.unix.
  
  macOS:
  
      Make sure you have xcode command line tools installed then run
      make -f Makefile.unix.
  
  Windows:
  
      Make sure you have mingw installed and the location within your PATH variable then run
      mingw32-make.exe -f Makefile.w32.

## About

  Chimaera OS is an operating system built from the ground up for
  Intel I386 and newer.
  
  The plan is to eventually have a fully fledged Unix-like system
  that's also capable of running Windows NT and DOS executables.
  Currently only a subset of NT is implemented and a select set
  of DOS exectuables has been tested.  When it comes to DOS
  applications that require a specific version you can simply
  do MSDOS_VER=x.xx and if you want to limit it to a specific
  directory you can use MSDOS_ROOT=xx.  There's also experimental
  ARMv7 support and [bash](https://github.com/Inknyto/arm-binaries/raw/refs/heads/main/bash/system/xbin/bash)
  has been partial tested.
  
  You might be wondering what the difference is between Chimaera and
  other operating systems.  Here's a few examples:
  
  ### BSD
  
    BSD started its life as Unix through licensing but they had to slowly
    move away from the Unix source code due to copyright laws and
    lawsuits.  BSD has split into three distinct flavors which are
    FreeBSD, OpenBSD and NetBSD but they all share the fact that they
    appear to be POSIX-centric meaning that if something isn't
    defined by a POSIX standard then chances are it's not going to be
    available.
    
    Chimaera on the other hand is eventually going to be POSIX first but
    will also adopt features if they could be really useful in the long
    run.  It's not going to adopt every GNU feature but will have a
    select few that may make the difference between say 3 lines and 5.
  
  ### Linux
  
    Right before anything let's get one thing straight, even though
    people refer to Debian, Ubuntu, Manjaro, etc as Linux they
    technically aren't as Linux is literally just the kernel.  Those
    examples are distributions that pack the Linux kernel along with GNU
    utilities and others.  With that being said the comparison should be
    with Corazon rather than Chimaera as a whole.
    
    Linux has a tendency to drop support for older systems, yes it's
    gradual but it still means that if you have a system from say 1990
    then you're not able to run the latest version of the kernel.
    
    Corazon on the other hand is and will always be I386 first and
    potentially newer CPUs second if the specific feature provides
    meaningful benefits.
  
  ### ReactOS
  
    ReactOS aims to reimplement the Windows NT architecture and behaviour,
    resulting in many of the same internal abstractions and layers found
    in Windows.  Chimaera instead targets compatibility at the interfaces
    required by applications.  Where an NT abstraction is unnecessary for
    compatibility, Chimaera may map an operation directly onto its own
    native facilities rather than reproducing the corresponding NT
    implementation.  An example of that could be:
    
    ReactOS:
    
      malloc -> RtlAllocateHeap -> NtAllocateVirtualMemory -> Kernel
    
    Chimaera:
    
      malloc -> NtAllocateVirtualMemory -> Kernel

## System Requirements

|     CPU     |   I386  |  AMD64  |
|-------------|---------|---------|
| Memory      |  16 MB  |  64 MB  |
| Firmware    | BIOS / EFI | BIOS / EFI |
| Display     | CGA / EGA / VGA / VBE / EFI&nbsp;FB | CGA / EGA / VGA / VBE / EFI&nbsp;FB
| Secure Boot | No | No |
