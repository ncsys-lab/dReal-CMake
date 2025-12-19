# todo: actual build/env instructions...

In the meantime, here is the environment(s) I'm using on Mac and/or CentOS. Don't copy and paste any of this, this is just for context on how you might want to setup your system for native builds.
Alternatively, you can just use Docker. todo: upload my Dockerfiles.

On mac, you need to use Rosetta since CAPD (for ODEs) is x86 only.
All compilers, dependencies, tooling, etc. needs to be ran through a separate rosetta environemnt.
Start by re-downloading and installing x86 homebrew— you will probably have two homebrews on your Mac now. 1 is a ARM, 1 is Rosetta— in different paths.
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
# .bashrc

# Source global definitions
if [ -f /etc/bashrc ]; then
	. /etc/bashrc
fi

# Uncomment the following line if you don't like systemctl's auto-paging feature:
# export SYSTEMD_PAGER=

# User specific aliases and functions

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

# separate line, because python/3.12 loads gcc/12
ml gcc/14.2.0
# ml llvm/17.0.6

source ~/.gcc_SOURCE_ME.sh

export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:${HOME}/usr/local/"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${HOME}/usr/local/lib/"
export PATH="$PATH:${HOME}/usr/local/bin/"
```

On CentOS I'm using
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

