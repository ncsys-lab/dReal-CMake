# Docker Build Instructions (Recommended)

## Build

**ARM64 (native Apple Silicon — recommended, ~5 min):**
```
docker build --platform linux/arm64 -t dreal/my_dreal_image:arm64 -f Dockerfile.dreal_ubuntu .
```

**x86_64 (emulated on Apple Silicon — slower, ~15 min):**
```
docker build --platform linux/amd64 -t dreal/my_dreal_image:1.0 -f Dockerfile.dreal_ubuntu .
```

> **Note:** If you see "Bad file descriptor" errors during build, add `--jobs 1` to the cmake build commands (Docker on macOS filesystem bug with parallel builds).

## Execute
```
cat YOUR_QUERY.smt2 | docker run --platform linux/arm64 --rm -i dreal/my_dreal_image:arm64 ./dreal4 --in --model
cat YOUR_QUERY.smt2 | docker run --platform linux/amd64 --rm -i dreal/my_dreal_image:1.0  ./dreal4 --in --model
```


# Native Build Instructions (macOS / Linux)

## macOS (ARM64 or x86_64)

Install dependencies via Homebrew:
```
brew install bison flex gmp cadical eigen cmake
```

Then build:
```
./FULL_BUILD.sh        # first build (creates gcc_build/)
./BUILD.sh             # subsequent builds
```

The binary is at `gcc_build/dreal4`. CMakeLists.txt auto-detects Homebrew paths.

## Linux (Ubuntu 24.04 / noble)

Install dependencies:
```
apt-get install -y clang cmake git bison flex libgmp-dev libeigen3-dev
```

Build CaDiCaL 3.0.0 from source (not yet packaged on Ubuntu):
```
git clone --branch rel-3.0.0 https://github.com/arminbiere/cadical
cd cadical && ./configure && make -j8
mkdir include lib && cp src/cadical.hpp include/ && cp build/libcadical.a lib/
export CMAKE_PREFIX_PATH="$PWD:$CMAKE_PREFIX_PATH"
```

Then build dreal4:
```
./FULL_BUILD.sh
```

On other Linux distros, set up equivalent dependencies and run `./FULL_BUILD.sh`.
