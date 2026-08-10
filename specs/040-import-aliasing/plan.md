# U8 plan — import aliasing (`import x as y;`)

**Epic**: modules-v2-graph · **Unit**: U8 (stretch) · **Covers**: REQ (DC10) · **Closes**: done-condition 10.
**Depends on**: U6a/U6b (the import graph + qualified resolution + name-capability).

## Behavior

`import x as y;` binds module `x` to the **local qualifier** `y`. After it:
- `y.foo(...)` is the qualified access path (functions stay qualified-only, U6a).
- D7 **name-capability** rides on the alias: `x`'s exported types are namable
  (`Pair<int>`), owned by `y` for the unused-import lint.
- The original name `x` is **not** also bound — the alias is a **rebind**, not an
  additional binding (D8: no implicit re-export / dual binding). `x.foo` is a
  located error.

## Mechanism

- **Parser** (`QModule.cpp`): `as` is a **contextual keyword** (lexes as a plain
  SYMBOL), parsed after the module name. `ImportStatement` carries the real module
  (`getModuleName()`) + the alias (`getAlias()`); `getLocalQualifier()` returns the
  alias, else the module name.
- **Resolution** (import handler): find the module's real namespace, register it
  **also under the alias** (`Scope::addNamespace(y, ns)`), `addImportedModule(y)`,
  record `y→x` (`Scope::addModuleAlias`), and `importTypeNamesFrom(ns, y)` (owner =
  the local qualifier).
- **Codegen-prefix fork** (`QExpression.cpp`): a qualified call's emitted symbol is
  unchanged for a `.bmod`-dep callee (extern → unprefixed → alias-independent, links
  from the `.a`); for a **combined-stdlib** callee the module prefix must be the
  REAL module, so it uses `Scope::realModuleName(qualifier)` (`s.args()` →
  `@sys__args`, not `@s__args`).
- **Diagnostics** (`qcc.cpp` import pass): the unknown-module check verifies the
  REAL module exists; the unused-import lint + message key on the local qualifier.

## Fixtures

- **Positive** (`test_build/aliasapp`): `import mathlib as m;` builds, links, and
  RUNS — `m.add`/`m.multiply`/`m.largest` link from `mathlib.a`, `Pair<int>` via
  name-capability under the alias. Exact-output check.
- **Negative** (`fail/xmodule/alias_rebind`, both build modes): `import lib as l;`
  then `lib.greet(...)` → located `undefined symbol 'lib'` (the alias rebinds; the
  original name is unbound — D8).

## Status

**LANDED.** Gates green both modes: `run_tests` 244/0 + 237/0, `test_codegen`
168/0, `--leak-check` Leaks:0, `test_lsp` 63/0, `test_build` all pass. Aliased
combined-stdlib prefix verified at the IR level (`@sys__args`).
