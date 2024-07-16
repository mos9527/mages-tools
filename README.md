# mages-tools
Cross-platform MAGES. (and N2System too, actually) engine Modding tools.

## Building
You'd need [cmake](https://cmake.org/download/) and a C++20 compliant compiler to build the tools.
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Download
You can download the latest release from the [releases page](https://github.com/mos9527/mages-tools/releases)

## (Un)packers
These tools are designed to be used from the command line, with the following syntax:
- unpacking: `<toolname> -i <input packed file> -o <output directory for unpacked files>`
- repacking: `<toolname> -r <output repacked file> -o <input directory for unpacked files>`

### [cpk](https://github.com/mos9527/mages-tools/blob/main/src/cpk.cpp)
*Probably* general-purpose, fast CriWare CPK file packer/unpacker.
#### Applicable games
- Chaos;Head Noah (Steam)
#### Untested games
- Stein;Gate 0 (PS3)

### [mpk](https://github.com/mos9527/mages-tools/blob/main/src/mpk.cpp)
MAGES. package file packer/unpacker.
#### Applicable games
- Steins;Gate (Steam)
- Steins;Gate 0 (Steam)
#### Untested games
- Chaos;Child (Steam)

# References
- https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/master/MikuMikuLibrary/Archives/CriMw/CpkArchive.cs
- https://github.com/wmltogether/CriPakTools/blob/mod/LibCPK/CPK.cs
- https://github.com/kamikat/cpktools/blob/master/cpk/crilayla.py