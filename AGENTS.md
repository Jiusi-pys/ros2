# Repository Guidelines

## Project Structure & Module Organization
This repository is a ROS 2 workspace manifest, not a single package. The tracked root files are:

- `ros2.repos`: `vcstool` manifest for the Jazzy workspace.
- `pixi.toml`: Windows-oriented dependency environment for building ROS 2.
- `.github/workflows/pr.yaml`: CI that validates manifest formatting and repository entries.
- `src/.gitkeep`: placeholder for the workspace source tree.

Use `vcs import src < ros2.repos` to populate `src/<org>/<repo>`. Note that the root `.gitignore` ignores `src/*`, so changes inside imported package repos are not part of this repository and should usually be contributed upstream in the relevant child repo.

## Build, Test, and Development Commands
- `vcs import src < ros2.repos`: clone or refresh the package set defined by this workspace.
- `vcs validate --input ros2.repos`: verify every repository URL and version entry.
- `yamllint ros2.repos -d "{extends: default, rules: {document-start: {present: false}, key-ordering: {}}}"`: run the same formatting check as CI.
- `pixi install`: create the Windows dependency environment from `pixi.toml`.
- `pixi shell`: enter that environment before running `colcon build` or `colcon test` on Windows.

## Coding Style & Naming Conventions
Keep YAML in `ros2.repos` compact, two-space indented, and organized by repository path (`ros2/rclcpp`, `ros-perception/image_common`). Preserve existing key order: `type`, `url`, then `version`. For `pixi.toml`, follow TOML conventions already used here: lowercase section names, quoted strings, and version pins with brief comments only when they explain a real constraint.

## Testing Guidelines
This repository has one repository-level gate: manifest validity. Run `yamllint` and `vcs validate` before opening a PR. There is no root coverage target here; package-level tests, linters, and coverage rules live in the imported repositories under `src/`.

## Commit & Pull Request Guidelines
Recent history favors short, imperative summaries with scope when helpful, for example `Add a pixi.toml file for installation on Windows.` or `[jazzy] Change urdfdom branch`. Include the distro or backport context when relevant. PRs should explain why a manifest or environment change is needed, link the upstream package or issue, and mention the validation commands you ran. Screenshots are unnecessary unless the change affects documentation rendering.
