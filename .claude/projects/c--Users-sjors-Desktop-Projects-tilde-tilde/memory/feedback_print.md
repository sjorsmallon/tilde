---
name: Use std::print not printf
description: User prefers std::print over printf for logging/output
type: feedback
---

Use `std::print` (C++23 `<print>`) instead of `printf` for all output/logging.

**Why:** User preference for modern C++ style. The project uses C++23.

**How to apply:** Any time you add debug output, logging, or console prints, use `std::print` / `std::println` instead of `printf`.
