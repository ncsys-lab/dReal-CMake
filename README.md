For compilation and execution of new checks:
run these from llvm-project
```
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCLANG_TIDY_ENABLE_STATIC_ANALYZER=OFF
ninja -C build clang-tidy
python3 ~/dReal-CMake/llvm-project/clang-tools-extra/clang-tidy/tool/run-clang-tidy.py -checks='-*,readability-prevent-using-ibex'
```