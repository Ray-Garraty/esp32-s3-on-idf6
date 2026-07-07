---
type: Architecture Decision
title: Migration Rationale
description: Rationale for migrating from Rust to C++23 for the ecotiter firmware on ESP-IDF v6
tags: [migration, architecture, rust, c++23]
timestamp: 2026-07-07
---

# Migration Rationale: Rust -> C++23

## Why Migrate?

| Aspect | Rust (legacy/rust) | C++23 (new) | Rationale |
|--------|-------------------|-------------|-----------|
| Language | Rust 2021 (nightly) | C++23 (ISO standard) | Stable toolchain, broader team familiarity |
| Framework | esp-idf-hal (Rust bindings) | ESP-IDF native C API | Zero abstraction overhead, direct HW access |
| Build system | Cargo + build.rs + git patches | CMake + idf.py (native) | No git patching, first-class ESP-IDF support |
| Toolchain | Custom Xtensa Rust fork | ESP clang (official) | Official vendor toolchain, always up-to-date |
| Async | std::thread (no async) | std::thread (no async) | Same model, same GR-1 rule |
| Error handling | Result<T, E> + thiserror | std::expected<T, E> | Idiomatic C++23, similar ergonomics |
| Memory | heapless::Vec, heapless::String | std::array, fixed buffers | Same budget discipline |

## Key Differences

### What Stays the Same
- 4-layer architecture (domain -> application -> infrastructure -> interface)
- All GR rules from AGENTS.md
- Thread model (7 dedicated threads, no async)
- Hardware pinout, state machines, protocol
- Memory budget (no heap in hot paths)
- Pre-flight checklist, crash investigation pipeline

### What Changes
- **Error handling:** Rust `Result<T, E>` + `?` operator -> C++ `std::expected<T, E>` + explicit checks
- **Ownership:** Rust borrow checker -> C++ RAII + delete copy
- **Lifetime safety:** Rust `'a` lifetimes -> C++ move semantics + `std::unique_ptr`
- **Unsafe:** `unsafe` blocks with `// SAFETY:` -> `_raw`/`_isr` naming + `// CONTRACT:` comments
- **Serialization:** serde derive macros -> nlohmann_json or manual JSON
- **Testing:** proptest + `cargo test` -> Catch2 v3 + ctest
- **Build:** Cargo + build.rs -> CMake + idf.py

## What We Keep from Rust Experience

- Diagnostic-first development (diag subsystem first)
- GR-1 through GR-7 (proven by 22 crash post-mortems)
- Stack budget table (GR-6) enforced at code review
- Stop flags for all RMT motion (GR-2)
- DRAM init order triangle (GR-3)
- No blocking in main loop (GR-1)

## What We Leave Behind

- Unstable nightly Rust toolchain
- Git-patched dependencies for IDF v6 compatibility
- `build.rs` linker script propagation hacks
- Rust `unsafe` for FFI calls (C API is inherently safe from C++)
