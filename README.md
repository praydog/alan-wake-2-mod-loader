# alan-wake-2-mod-loader

A mod loader for Alan Wake 2 that enables easy installation of mods by allowing modded files to be loaded directly from the Windows filesystem, rather than big packaged files.

[Modding Haven Discord](https://discord.gg/9Vr2SJ3)

## Installation

Extract the zip on the [Releases](https://github.com/praydog/alan-wake-2-mod-loader/releases/) page into your AlanWake2 directory.

## Installation of loose file mods

Can usually just extract any mod into the game directory with its original RMDTOC folder structure.

![WinRAR_2023-11-20_19-01-40](https://github.com/praydog/alan-wake-2-mod-loader/assets/2909949/fcb16b62-9aca-4185-8ee6-0d3d276fef95)

## Custom command line arguments

* `-logpack2`
    * Log files that are being actively accessed by the game
* `-logpack2_exts "ext"`
    * Log files with specific extensions
    * e.g. `-logpack2_exts "wem,tex"`
    * Must have quotes

## Limitations

Unknown at this time. Tested on a few mods but not aware of any issues yet.
