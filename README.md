<div align="center">
  <h1 align="center">lizardbyte-common</h1>
  <h4 align="center">Common helper scripts and repository tooling for LizardByte projects</h4>
</div>

<div align="center">
  <a href="https://github.com/LizardByte/lizardbyte-common/actions/workflows/ci.yml?query=branch%3Amaster"><img src="https://img.shields.io/github/actions/workflow/status/lizardbyte/lizardbyte-common/ci.yml.svg?branch=master&label=CI&logo=github&style=for-the-badge" alt="CI"></a>
  <a href="https://app.codecov.io/gh/LizardByte/lizardbyte-common"><img src="https://img.shields.io/codecov/c/gh/LizardByte/lizardbyte-common.svg?style=for-the-badge&logo=codecov&label=codecov" alt="Codecov"></a>
</div>

## Overview

This repository contains shared helper scripts and repository-level tooling used across LizardByte projects.

The current tooling is focused on Python-managed C/C++ formatting helpers:

- `scripts/update_clang_format.py` runs `clang-format` across supported source directories.

## Python Tooling

Install [uv](https://docs.astral.sh/uv/) and sync the locked tool environment:

```bash
uv sync
```

Run the clang-format helper:

```bash
uv run python scripts/update_clang_format.py
```

## Tests

Run the pytest suite:

```bash
uv run pytest
```
