NEStalgia
=========

NEStalgia, a Nintendo Entertainment System (NES) Emulator.  By Payton Quackenbush.

Written in C99.

Platforms supported
-------------------
- OSX
- Linux

Screenshots
===========
[Zelda Video](https://vimeo.com/90365281)

![](http://i.imgur.com/ZBcbAmw.png)
![](http://i.imgur.com/2MGUfOA.png)
![](http://i.imgur.com/BSLNRcw.png)

Implements
----------
- 8-bit N6502 CPU
- PPU (picture processing unit)
- APU (audio processor)
- Mappers 0, 1, 2, 4, 7, and 87

Requirements
------------
- GCC/Clang toolchain
- GNU Make
- SDL library for Graphics and Audio

Compilation Instructions
------------------------
To compile and run in one step:

```
% ./bin/nes PATH_TO_ROM
```

To compile and run manually:
```
% make
% ./build/PLATFORM/nestalgia PATH_TO_ROM
```

Games supported
---------------
- Arkanoid
- Castlevania
- Contra
- Deja Vu
- Double Dragon 1, 2
- Donkey Kong
- Dr. Mario
- Dragon Warrior 1, 2, 4
- Excite Bike
- Final Fantasy 1
- Galaga
- Maniac Mansion
- Super Mario 1, 2
- Tecmo Bowl
- Tetris
- Zelda
- Metroid
