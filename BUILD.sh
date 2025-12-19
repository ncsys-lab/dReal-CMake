#!/usr/bin/env bash
cd "$(dirname "$0")"


cd gcc_build &&
# cmake -DCMAKE_BUILD_TYPE=Release .. &&
cmake --build . --target dreal4 -j8 &&
cd ../ &&
echo -e "\a DONE! \a"

