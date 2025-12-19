# Docker Build Instructions (Recommended)
### Build
NOTE: there is a filesystem bug in docker on MacOS, if you get some build error related to "Bad" file discriptors then change all `-j` flags in FULL_BUILD.sh and CMakeLists.txt to `-j1` (disable parallelism).

In this directory, run:
```
docker build --platform linux/amd64 -t dreal/my_dreal_image:1.0 -f Dockerfile.dreal_ubuntu .
```

This will probably take 20+ minutes.

### Execute
```
cat YOUR_QUERY.smt2 | docker run --platform linux/amd64 --rm -i dreal/my_dreal_image:1.0 ./dreal4 --in --model
```


# Native Build Instructions (Not Recommended on Mac)
NOTE: This is tedious on Linux, and very tricky on M1 Macs. Lots of manual environment setup is required.

Todo: Write actual build instructions... In the meanwhile, here are the environment(s) I'm using on Mac and/or CentOS. Don't copy and paste any of this, this is just for context on how you might want to setup your system for native builds.

On mac, you need to use Rosetta since CAPD (for ODEs) is x86 only.
All compilers, dependencies, tooling, etc. (including whatever CMake/Make/Ninja runs underneath you) needs to be ran through Rosetta (`arch -x86_64`).
Start by re-downloading and installing x86 homebrew— you will probably have two homebrew executables (in different paths) on your Mac now. One for ARM, and one for x86/Rosetta.
These homebrews operate independently, be careful which one you are running.
On mac, I used x86 homebrew to get:
```
==> bison: stable 3.8.2 (bottled) [keg-only]
==> cmake: stable 4.2.1 (bottled), HEAD
==> flex: stable 2.6.4 (bottled), HEAD [keg-only]
==> gmp: stable 6.3.0 (bottled), HEAD
==> cadical: stable 2.2.0 (bottled)
```


My CentOS `.bashrc` file (relevant paths should be configured similarly for Mac):
```
...

# Dependencies in my environment provided by package manager...
ml python/3.12.1 \
   java/11.0.11 \
   git/2.45.1 \
   ninja/1.9.0 \
   make/4.4 \
   cmake/3.31.4 \
   gdb/8.2.1 \
   glib/2.52.3 \
   wget/1.25.0 \
   valgrind/3.14.0 \
   py-pandas/2.2.1_py312 \
   py-numpy/1.26.3_py312 \
   py-scipy/1.12.0_py312 \
   py-tables/3.10.1_py312 \
   protobuf/29.1


ml gcc/14.2.0

source ~/.gcc_SOURCE_ME.sh

export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:${HOME}/usr/local/"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${HOME}/usr/local/lib/"
export PATH="$PATH:${HOME}/usr/local/bin/"
```

On CentOS I'm using:
```
ks1@sh04-ln02 ~> cat ~/.gcc_SOURCE_ME.sh
export CXX="YOUR/PATH/HERE/gcc/14.2.0/bin/g++"
export CC="YOUR/PATH/HERE/gcc/14.2.0/bin/gcc"
```

On MacOS, make sure you're running:
```
Apple clang version 17.0.0 (clang-1700.4.4.1)
Target: x86_64-apple-darwin25.1.0
```
This repo relies on some modern C++ features, so make sure your compiler is up-to-date if you're seeing lots of errors.


On linux, I manually built the following, from source, and placed their includes/libs/etc. in the directories specified under my `CMAKE_PREFIX_PATH`/`LD_LIBRARY_PATH`/`PATH` environment variables:
```
bison-3.8.2
cadical-rel-2.1.3
flex-2.6.4
gmp-6.3.0
```

On Mac, you can get them from homebrew... CMakeLists.txt should pick them up automatically, so you may or may not need to manually set `CMAKE_PREFIX_PATH`/`LD_LIBRARY_PATH` like I did on linux.

Once your environment, paths, and dependencies are setup, you can launch CMake configuration and build using `./FULL_BUILD.sh`.
You can launch subsequent builds using `./BUILD.sh`, which will skip the CMake configuration step.
The executable will be found in `gcc_build/dreal4`.