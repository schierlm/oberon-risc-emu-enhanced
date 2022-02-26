# Oberon RISC Emulator Enhanced

This is a fork of Peter de Wachter's [Oberon RISC Emulator](https://github.com/pdewacht/oberon-risc-emu).
It adds the following features:

- [Hardware Enumerator](https://github.com/schierlm/OberonEmulator/blob/master/hardware-enumerator.md) support
- RISC Interrupt support
- PCLink works for binary files on Windows platform
- Use Drag & Drop for sending files to Oberon via PCLink
- Truncate filesystem images when requested from within Oberon
- Provide the current wall clock time via Hardware Enumerator, if enabled
- 4-bit and 8-bit color modes, with dynamic and seamless resizing
- HostFS Filesystem
- Host Transfer support
- Debug Console support (a bit more convenient than 8 LEDs for kernel debugging)


## Description from the original README

This is an emulator for the Oberon RISC machine. For more information,
[see Niklaus Wirth's site](https://www.inf.ethz.ch/personal/wirth/). For
newcomers to the Oberon family of operating systems, the document
[Using Oberon] in the [Project Oberon section] is a must-read.

[Using Oberon]: https://www.inf.ethz.ch/personal/wirth/ProjectOberon/UsingOberon.pdf
[Project Oberon section]: https://www.inf.ethz.ch/personal/wirth/ProjectOberon/index.html

![Screenshot](po2013.png)

## Building

To build the emulator, you need the SDL2 library and a C compiler that
understands C99 (GCC and clang are fine).

[SDL2]: http://libsdl.org/

The release is continuously built using GitHub Actions. You can grab
binary snapshots for Windows and macOS from there.

### Linux

To install the needed packages on Debian, Ubuntu and derived
distributions, use this command:

    sudo apt-get install build-essential libsdl2-dev

See your distribution's documentation if you're using something else.

After that, build the emulator using the command `make`.

### macOS

There's a pre-compiled version in Github's Releases section.

I can't give much support for macOS, but I've had many reports saying
it works fine. The main stumbling block seems to be that there are two
ways to install the SDL development files: Unix style and Xcode style,
as explained in the [SDL Mac OS X FAQ].

For Unix style, build using the command `make`.
For Xcode style, use `make osx`.

[SDL Mac OS X FAQ]: https://wiki.libsdl.org/FAQMacOSX

### Windows

There's a pre-compiled version in Github's Releases section, cross compiled from Linux.

See the [SDL site][SDL2]  for how to set up a compiler
for Windows. It's fiddly.

Alternatively, you can set up a cross compiler from Linux, which is
also rather fiddle, and build with a command such as: (This is mostly
for my own future reference.)

    make CC=i686-w64-mingw32-gcc-win32 \
         SDL2_CONFIG=/usr/local/cross-tools/i686-w64-mingw32/bin/sdl2-config


## Disk image

If you don't know what disk image to use, you probably want to use one of the disk images
from the [Oberon2013Modifications](https://github.com/schierlm/Oberon2013Modifications). The
images from the original emulator should be fine too, as well as every image that either supports
the Hardware Enumerator or runs on Wirth's original board.


## Command line options

Usage: `risc [options] disk-image.dsk`

* `--fullscreen` Start the emulator in fullscreen mode.
* `--mem <megs>` Give the system more than 1 megabyte of RAM.
* `--rtc` Provide the current wall clock time via Hardware Enumerator
* `--size <width>x<height>[x<depth>]` Use a non-standard window size and/or color depth
* `--dynsize` Allow dynamic screen resize from guest
* `--hostfs <directory>` export files inside DIRECTORY as HostFS (requires a different inner core on disk)
* `--hosttransfer` Allow the guest to request file transfers from the host
* `--leds` Print the LED changes to stdout. Useful if you're working on the kernel,
  noisy otherwise.

## Keyboard and mouse

The Oberon system assumes you use a US keyboard layout and a three button mouse.
You can use the left alt key to emulate a middle click.

The following keys are available:
* `Alt-F4` Quit the emulator.
* `F11` or `Shift-Command-F` Toggle fullscreen mode.
* `F12` Soft-reset the Oberon machine.


## Transferring files

First start the PCLink1 task by middle-clicking on the PCLink1.Run command.
Transfer files using the pcreceive.sh and pcsend.sh scripts.

You can also drag files onto the emulator window to transfer them into the emulator, if PCLink is running.

Alternatively, use the clipboard integration to exchange text.

You can also use Host Transfer to initiate file transfers from/to the host on the guest.


## Clipboard integration

The Clipboard module provides access to the host operating system's
clipboard using these commands:

* `Clipboard.Paste`
* `Clipboard.CopySelection`
* `Clipboard.CopyViewer`

## Copyright

Copyright © 2014 Peter De Wachter
Copyright © 2018-2023 Michael Schierl

Permission to use, copy, modify, and/or distribute this software for
any purpose with or without fee is hereby granted, provided that the
above copyright notice and this permission notice appear in all
copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
