# Cest v5 — API Reference (in-repo copy)

Condensed reference for the **Cest** testing framework (v5), for use by
Claude Code without fetching the website. Authoritative source:
https://cestframework.com/reference/ . Cest is MIT-licensed and
header-only; the test runner is a separate binary. Both are fetched from
the v5 GitHub release by `scripts/init.sh` (see testing-guidelines.md).

## Test definition

### Building blocks
- A test file has **one top-level `describe`** (sibling top-level describes
  in one file are not supported — one per file).
- `describe("name", []() { ... })` — a suite; may nest `describe` blocks.
- `it("name", []() { ... })` — a test case.
- Nested describes run outside-in.

```cpp
describe("Socket", []() {
  describe("send()", []() {
    it("sends data", []() {});
  });
  it("does nothing", []() {});
});
```

### Focus / skip / pending
- `xit(...)` skip a test; `fit(...)` run only this test.
- `xdescribe(...)` skip a suite; `fdescribe(...)` run only this suite.
- `todo("description")` mark a pending (unimplemented) test.

### Pre/post conditions
Order: `beforeAll` → `beforeEach` → test → `afterEach` → `afterAll`.
**Only one of each per suite.** Capture outer state by reference `[&]`.

```cpp
int *data = nullptr;
describe("pre/post", [&]() {
  beforeEach([&]() { data = new int; *data = 0; });
  afterEach([&]()  { delete data; });
  it("uses data", [&]() { expect(*data).toEqual(0); });
});
```

## Assertions

Form: `expect<T>(value).matcher(...)`. Negate with `.Not->`:
```cpp
expect(123).toBe(123);
expect(123).Not->toBe(321);
```
Custom types need the relevant `operator==` / `operator<` etc., unless a
specialization or a custom matcher is provided.

### Generic (any T)
| Matcher | Passes if |
|---------|-----------|
| `toBe(expected)` / `toEqual(expected)` | `value == expected` |
| `toBeTruthy()` / `toBeFalsy()` | `value` / `!value` |
| `toBeGreaterThan(x)` / `toBeLessThan(x)` | ordering via `>` / `<` |
| `toBeInRange(min, max)` | `min <= value <= max` |
| `toEqualBytes(expected)` | byte-equal via `memcmp` (C structs w/o `operator==`) |
| `toHaveBitsSet(mask)` | `(value & mask) == mask` |
| `toHaveBitsClear(mask)` | `(value & mask) == 0` |

`toEqualBytes` and `toHaveBitsSet/Clear` are directly useful for VGM byte
buffers and chip register/bitfield checks.

### Floating point (float/double)
`toBe(expected, epsilon)` / `toEqual(expected, epsilon)` — passes if
`fabs(actual-expected) <= epsilon`. Default ε = 1e-4 (float) / 1e-6
(double). Use this for the pitch tests (fnum/frequency tolerance).

### Strings (std::string)
`toBe/toEqual`, `toMatch("substr")`, `toMatch(Regex("^re$"))`,
`toHaveLength(n)`.

### Pointers (T*)
| Matcher | Passes if |
|---------|-----------|
| `toBeNull()` | pointer == 0 |
| `toBeNotNull()` | pointer != 0 |
| `toEqualMemory(expected, length)` | byte-equal over `length` bytes |

`toEqualMemory` is the natural way to compare a produced VGM byte range
against an expected buffer.

### Collections & std types
Specializations exist for `std::vector` (`toBe`, `toContain`,
`toHaveLength`), `std::array`, C-style arrays (no pointer decay), `list`,
`forward_list`, `deque`, `set`/`unordered_set` (`toInclude`, `toHaveSize`),
`multiset`, `map`/`unordered_map` (`toHaveKey`, `toInclude`, `toHaveSize`),
`multimap`, `pair`, `tuple`, `optional` (`toHaveValue`, `toBeEmpty`),
`bitset` (`toHaveBitSet`, `toHaveCount`, `toHaveAll`, `toHaveNone`), plus
smart pointers, `complex`, `chrono::duration` (`toBeCloseTo`), and
`filesystem::path`. See the website for the full per-type tables; the
generic + pointer + float + string matchers above cover most of this
tool's needs.

### Exceptions
Callable form (preferred):
```cpp
expect([]() { readFile(""); }).toThrow();
expect([]() { readFile(""); }).toThrow<std::runtime_error>();
expect([]() { readFile(""); }).toThrowMessage("Bad path!");
expect([]() { readFile("/tmp/x"); }).Not->toThrow();
```
Legacy: `assertThrows<E>([=]() { ... });`.
(The tool uses `ErrorCode` out-params, not exceptions, for expected
failures — so exception matchers are mainly for guarding against
unexpected throws.)

### Custom assertions
Specialize `Assertion<MyType>` and `expectFunction<MyType>` (must provide a
`Not` sibling in the ctor and XOR the check with `negated`). Full example:
https://github.com/cegonse/cest/blob/master/test/examples/test_custom_assertions.cpp
Custom types no longer need `operator<<`; non-printables show as
`<non-printable>`.

## Parametrized tests
```cpp
struct Case { int a, b, result; };
describe("Calculator", []() {
  it("adds", []() {
    withParameter<Case>()
      .withValue(Case{1,1,2})
      .withValue(Case{2,3,5})
      .thenDo([](Case x) { expect(x.a + x.b).toEqual(x.result); });
  });
});
```
Ideal for table-driven checks (e.g. a set of KC/KF → fnum/block reference
rows, or note-code → semitone mappings).

## Test executable CLI (each compiled test binary)
`-h/--help`, `-r/--randomize`, `-s/--seed <n>`, `-g/--grep <pattern>`,
`-o/--only-suite-result`, `-t/--tree-suite-result`, `-j/--json`,
`-l/--print-test-list`.

## Cest Runner CLI (`cest-runner`)
Runs/aggregates multiple test binaries in a directory.
- `[directory]` — where to look for tests (default `$CWD`).
- `--watch` — interactive watch mode.
- `--grep <pattern>` — only run matching test files/cases.

## Signals
The runner captures and reports fatal signals as test failures. On
Linux/macOS: `SIGSEGV`, `SIGFPE`, `SIGBUS`, `SIGILL`, `SIGTERM`, `SIGXCPU`,
`SIGXFSZ` (reported like `Killed by signal 11 (Segmentation fault)`). This
means a crash in the tool under test is surfaced as a failing test, not a
lost run — useful when fuzzing malformed VGM input.

## Leak Sanitizer integration
The runner detects ASan/LSan via the `__SANITIZE_ADDRESS__` define (set by
the compiler when `-fsanitize=address` is on). When enabled, leaks are
reported. Build the test targets with ASan to make the create/destroy pair
tests catch leaks (see testing-guidelines.md and the Cest CMake pattern).
