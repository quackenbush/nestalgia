NEStalgia
=========

NEStalgia, a Nintendo Entertainment System (NES) Emulator.  By Payton Quackenbush.

Written in C, specifically C99.

[Zelda Video](https://vimeo.com/90365281)

Implements:
- N6502 CPU
- PPU (picture processing unit)
- APU (audio processor)
- Many Mappers.

Requirements:
- SDL library for Graphics and Audio

Platforms supported
-------------------
OSX
Linux

Compilation Instructions
------------------------
To compile and run in one step:
% ./bin/nes PATH_TO_ROM

To compile and run manually:
% make
% ./build/PLATFORM/nes PATH_TO_ROM

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
- Galaga
- Maniac Mansion
- Super Mario 1, 2
- Tecmo Bowl
- Tetris
- Zelda
- Metroid
