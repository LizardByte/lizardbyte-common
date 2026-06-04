<div align="center">
  <h1 align="center">lizardbyte-common</h1>
  <h4 align="center">Common helper scripts and repository tooling for LizardByte projects</h4>
</div>

<div align="center">
  <a href="https://github.com/LizardByte/lizardbyte-common/actions/workflows/ci.yml?query=branch%3Amaster"><img src="https://img.shields.io/github/actions/workflow/status/lizardbyte/lizardbyte-common/ci.yml.svg?branch=master&label=CI&logo=github&style=for-the-badge" alt="CI"></a>
  <a href="https://codecov.io/gh/LizardByte/lizardbyte-common"><img src="https://img.shields.io/codecov/c/gh/LizardByte/lizardbyte-common?token=3jjWHYBrxB&style=for-the-badge&logo=codecov&label=codecov" alt="Codecov"></a>
</div>

## Overview

This repository contains shared helper scripts and repository-level tooling used across LizardByte projects.

The current tooling includes Python-managed helpers and reusable GitHub workflows:

- `scripts/update_clang_format.py` runs `clang-format` across supported source directories.
- `scripts/localize.py` updates gettext and Babel locale files.
- `.github/workflows/localize.yml` runs the locale helper from GitHub Actions and opens localization update pull requests.

## Python Tooling

Install [uv](https://docs.astral.sh/uv/) and sync the locked tool environment:

```bash
uv sync --locked
```

Run the clang-format helper:

```bash
uv run --locked python scripts/update_clang_format.py
```

Run gettext extraction:

```bash
uv run --locked --only-group locale python scripts/localize.py --extract
```

## Workflows

Reusable GitHub workflows live under `.github/workflows/`.

- `localize.yml` extracts gettext strings with the shared locale helper and can open a localization update pull request.

```yaml
name: localize
permissions: {}

on:
  push:
    branches:
      - master
    paths:
      - '.github/workflows/localize.yml'
      - 'src/**'
      - 'locale/sunshine.po'
  workflow_dispatch:

jobs:
  localize:
    name: Update Localization
    permissions:
      contents: read
    uses: LizardByte/lizardbyte-common/.github/workflows/localize.yml@master
    secrets:
      github_token: ${{ secrets.GH_BOT_TOKEN }}
```

## Tests

Run the pytest suite:

```bash
uv run --locked --only-group test-python pytest
```
