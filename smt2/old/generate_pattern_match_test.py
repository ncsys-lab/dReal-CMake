import random


def generate_smt_script(N):
    print("(set-option :precision 1e-03)")

    for i in range(1, N + 1):
        # lower = round(random.uniform(0.2, 1.1), 2)
        # upper = round(random.uniform(lower, min(lower + 0.1, 1.2)), 2)
        lower = 0.2
        upper = 1.2
        print(f"(declare-const x{i} Real [{lower}, {upper}])")
        print(f"(declare-const y{i} Real)")

    print("\n(assert (or")
    for i in range(1, N + 1):
        print(f"    (and (= y{i} (sin x{i})) (= y{i} (atan x{i})))")
    print("))\n")

    print("(check-sat)")
    print("(get-model)")
    print("(exit)")

if __name__ == '__main__':
    N = 1000
    generate_smt_script(N)
