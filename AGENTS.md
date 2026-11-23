# Simphony contribution guidelines

## Changes

- Keep each change focused and use the simplest implementation that meets the requirements. Reuse existing code and patterns before adding helpers, abstractions, or dependencies.
- Follow the surrounding style and repository formatting rules (`.clang-format` for C++ and CUDA). Keep unrelated refactors and formatting out of the diff.
- Preserve public API and configuration compatibility unless the change calls for a break. Update affected docs and examples, and explain any migration steps.

## Tests

- Always add focused unit tests for new features and regression tests for bug fixes. Test observable behavior and relevant edge cases, and keep tests deterministic.
- Register tests in the appropriate `CMakeLists.txt` so CTest runs them. Build with `cmake --build build`, then run `ctest --test-dir build --output-on-failure` (use `-R <pattern>` for focused runs). See the [README](README.md#build-from-source) for configuration and prerequisites.
- Run the checks relevant to the change before review. Report what ran, the results, and anything skipped, including checks that require a GPU.

## Commits and pull requests

- Use Conventional Commits (`type(scope): summary`), for example `fix(simg4ox): validate GPU selection`. Keep commits focused and use `BREAKING CHANGE:` to describe incompatible changes.
- Provide a PR-ready description that explains what changed and why, lists validation and results, and calls out compatibility or migration details when relevant. Link related issues when available.
