# Dread Remote Lua
A further modification of Dread depackager, that makes the game listen to a socket, running any lua code sent to it.

# Original readme 
# dread depackager
A modification for Metroid: Dread allowing redirection of files from within pkg files to loose files in RomFS.

# usage
dread deapackager expects a json file called "replacements.json" to be placed into the root of the RomFS directory for your mod, and the subsdk9 and main.npdm files in the exefs directory.

replacements.json can have two structures, and it will be automatically detected.

format 1 example:
```json
{
	"replacements" :
	[
		"file1/path/within/pkg",
		"file2/path/within/pkg"
	]
}
```

with format 1, `file1/path/within/pkg` will be directed to `rom:/file1/path/within/pkg` when the game tries to open it from within a pkg, and instead will open it from the same path within RomFS.

format 2 example:
```json
{
	"replacements" :
	[
		{ "file1/path/within/pkg" : "rom:/mymod/file1" },
		{ "file2/path/within/pkg" : "rom:/mymod/file2" }
	]
}
```

with format 2, the RomFS path is arbitrarily defined for any pkg file path, allowing for more flexible organization of the reaplced files in the finished mod

# How it works
Dread depackager uses the filepaths listed in replacements.json to selectively replace paths in the game's path to crc conversion code. 
All file paths first pass through this function, and by hooking it and replacing the string, it can selectively redirect file paths into romfs

dread depackager uses a few libraries:
 - exlaunch, a code injection framework for switch executables. Its original readme can be found below 
 - cJSON, a json parsing library written in C. Dread depackager uses a slightly modified version of this library.

# Original exlaunch readme 
## exlaunch
A framework for injecting C/C++ code into Nintendo Switch applications/applet/sysmodules.

## Note
This project is a work in progress. If you have issues, reach out to Shadów#1337 on Discord.

## Credit
- Atmosphère: A great reference and guide.
- oss-rtld: Included for (pending) interop with rtld in applications (License [here](https://github.com/shadowninja108/exlaunch/blob/main/source/lib/reloc/rtld/LICENSE.txt)).
