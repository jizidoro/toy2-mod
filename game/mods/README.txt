TOY STORY 2 - MODS
==================

Data mods (replace game files without touching the originals)
-------------------------------------------------------------
1. Make a folder here for your mod, e.g.  mods\MyMod\
2. Put replacement files in it with the SAME relative path the game uses:
     game data  ->  mods\MyMod\data\...   (e.g. mods\MyMod\data\chars\buzz.all)
     CD files   ->  mods\MyMod\cd\...     (music: cd\audio\*.wav, videos: cd\rtlibs\*.dll)
3. Start the game. Only the files you provide are replaced; everything else loads from the originals.

Which files can be replaced: after one play session, mods\ts2mods.log lists every file the game opened
("open ...") and every replacement it used ("REDIRECT ...").

Several mods: create mods\load_order.txt with one mod folder name per line; the FIRST line wins when two
mods replace the same file. Lines starting with # are ignored. Without load_order.txt, all folders here
are used in alphabetical order.

Disable a mod: remove its folder (or its line in load_order.txt).

Texture mods (HD textures)
--------------------------
Put PNG files in  mods\<MyPack>\textures\  named  <key>.png  (anything may follow the 16-digit key, e.g.
<key>_woodfloor.png). Any size works; the game stretches every texture to a power-of-two square, so a 4x upscale
of the original is the natural choice. Transparency comes from the PNG's alpha channel. Needs scripts\ts2tex.asi.
The same load_order.txt decides which pack wins when two replace the same texture.

Where the keys come from: create the folder game\texdump\ and play; every texture the game loads is written
there once as <key>.png (the original picture) with a line in texdump\index.csv. Delete the folder to stop.
All textures at once, without playing: py C:\toy_story_2\tools\ts2tex\scan.py  (-> work\texpack\src).
mods\hd-textures is the AI first pass (tools\ts2tex\upscale.py); to fix one texture by hand, put your version
in another pack listed before hd-textures in load_order.txt. To play with the original textures, delete the
hd-textures folder or move it out of mods\ (a renamed folder inside mods\ still loads); scripts\ts2tex.asi alone still gives 32-bit colour.

Code mods
---------
ASI plugins placed in game\scripts\ are loaded at startup (ToyStory2Fix.asi and ts2mods.asi work this way).

Tools for the file formats (.ngn, .raw, .all, .anm, .dat): see the Toy-Story-2-Modding and toy2-decomp
projects (C:\toy_story_2\Toy-Story-2-Modding, C:\toy_story_2\toy2-decomp).
