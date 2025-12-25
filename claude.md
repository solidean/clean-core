# Project guidance for LLMs

This file provides **biases and defaults, not laws**.
Local context and reviewer intent override it.
When in doubt, ask or follow surrounding code.

Its purpose is to help you make *locally correct, stylistically aligned decisions* when contributing to **clean-core**, a foundational C++ library with unusually high engineering standards.

Do not treat this as a checklist. Treat it as a compass.

---

## 1. Project Axioms (stable, non-negotiable biases)

* **Correctness and composability dominate cleverness.**
  Elegant code that weakens invariants is a regression.

* **Performance matters, but only where it can matter.**
  Hot-path code must be excellent; cold-path code must be cheap to compile and easy to reason about.

* **Compile time is a first-class cost.**
  Header weight, template instantiations, and ADL side effects are design concerns, not afterthoughts.

* **Undefined behavior is avoided by default.**
  Any intentional UB must be rare, localized, and heavily documented with rationale.

* **This library is foundational.**
  APIs favor predictability, explicitness, and long-term usefulness over short-term convenience.

If a change violates one of these, it likely needs a strong justification or a different design.

---

## 2. Vocabulary & Semantic Conventions

Certain words have *specific meanings here*:

* **“Safe”** means lifetime-safe and invariant-preserving, not thread-safe unless stated.
* **“Cheap”** usually means O(1), non-allocating, and header-only friendly.
* **“Hot-path”** means code plausibly inside tight user loops, not “called often in theory”.
* **“Invariant violation”** implies programmer error and is assertion territory.
* **“Expected failure”** implies `cc::result` or `optional`, not exceptions.

Do not assume standard-library connotations automatically apply.

---

## 3. Design Defaults (preferences, not bans)

Prefer these *unless context clearly argues otherwise*:

* **Default-constructible types** with explicit validity states over complex constructors.
* **Static factory methods** (`create_*`) for fallible or non-trivial construction.
* **Value types or move-only ownership** over shared ownership.
* **View types** (`span`, `string_view`, `function_ref`) for non-owning parameters.
* **Thin templates + type erasure** over deep template stacks.
* **`static_assert` for diagnostics**, `requires` for overload control.
* **Hidden friends** for operators to control ADL and compile times.
* **Headers that compile standalone**, even at some redundancy cost.

Avoid absolutism. Context can override defaults.

---

## 4. Things That Are Intentionally *Not* Optimized For

* Teaching C++ basics
* Mimicking the standard library
* ABI stability
* Exotic platforms or niche compilers
* Maximal constexpr or noexcept coverage
* Template metaprogramming for its own sake

If you feel tempted to “improve” one of these, assume it was considered already.

---

## 5. Known Sharp Edges (be extra careful)

* **Move assignment must be subobject-safe**, or explicitly documented otherwise.
* **ADL must be controlled.** Prefix `cc::` deliberately in templated contexts.
* **Assertions must be side-effect free.**
* **Debug vs Release behavior must not meaningfully diverge.**
* **Macros require justification.** Prefer language features whenever possible.

These are common sources of subtle bugs and regressions.

---

## 6. How to Behave When Unsure

When you are not confident:

* Prefer **asking a clarifying question** over guessing.
* Prefer **pointing out tradeoffs** over silently choosing one.
* Explicitly flag when a suggestion **deviates from local patterns**.
* Follow **nearby code** over abstract rules.

A correct question beats an incorrect patch.

---

## 7. Relationship to Coding Guidelines

The full **Coding Guidelines** document is the authoritative reference for:

* naming, formatting, and layout
* detailed error handling rules
* testing conventions
* build and platform constraints

This file exists to **compress intent**, not to duplicate rules.
If the two ever conflict, defer to human judgment and local context.

---

## 8. Update Contract

Edit this file only when:

* project philosophy changes
* recurring misunderstandings appear in reviews
* a new class of mistakes needs preemptive guidance

Do not update it for one-off style decisions.

---

### Mental model

Act like a senior contributor who:

* understands *why* the rules exist
* optimizes for long-term composability
* resists cleverness unless it pays rent

When in doubt, choose the design that would age best five years from now.

---

### Final blindspot check (before sending code)

* Does this change increase hidden coupling?
* Does it add compile-time or cognitive weight without clear payoff?
* Would this still make sense if wrapped by another abstraction?

If those feel solid, you are probably aligned.
