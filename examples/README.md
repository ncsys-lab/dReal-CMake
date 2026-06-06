# Examples

Reference programs demonstrating the dReal C++ API. These are **not** built by
`./BUILD.sh` — there are no CMake targets for them. They exist as code to read
when learning how to call `CheckSatisfiability`, `CheckLyapunov`, etc. See
`docs/api-guide.md` for the API itself.

| File | What it shows |
|---|---|
| `check_lyapunov.cc` | Verify a candidate Lyapunov function for a fixed system. |
| `synthesize_lyapunov_simple.cc` | Minimal Lyapunov synthesis (parameterized template + ∃∀ search). |
| `synthesize_lyapunov_damped_mathieu.cc` | Lyapunov synthesis for the damped Mathieu equation. |
| `synthesize_lyapunov_moore_greitzer.cc` | Lyapunov synthesis for a Moore–Greitzer jet-engine model. |
| `synthesize_lyapunov_normalized_pendulum.cc` | Lyapunov synthesis for a normalized pendulum. |
| `synthesize_lyapunov_power_train.cc` | Lyapunov synthesis for a power-train model. |
| `program_synthesis_abs.cc` | Sketch-style program synthesis for `abs`. |
| `verify_nn.cc` | Neural-network input/output verification. |
| `control.cc`, `control.h` | Helper utilities used by the Lyapunov examples (not standalone). |

## Compiling one manually

There is no project target, so build by hand against the installed `dreal`
library. Example using clang++ from the project root after `./FULL_BUILD.sh`:

```bash
clang++ -std=c++17 \
  -I src -I gcc_build/ibex-install/include \
  examples/check_lyapunov.cc examples/control.cc \
  -L gcc_build -ldreal \
  -L gcc_build/ibex-install/lib -libex \
  -o check_lyapunov && ./check_lyapunov
```

Adjust include/lib paths to match your build directory. The `Lyapunov` examples
share `control.h` / `control.cc`; compile both source files together for those.
