import importlib.util
import subprocess
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).with_name("freeze_ros2_repos.py")
SPEC = importlib.util.spec_from_file_location("freeze_ros2_repos", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def git(path: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


@pytest.fixture
def published_repo(tmp_path, monkeypatch):
    root = tmp_path / "workspace"
    remote = tmp_path / "remote.git"
    repo = root / "src" / "org" / "repo"
    (root / "patches").mkdir(parents=True)
    repo.parent.mkdir(parents=True)
    subprocess.run(["git", "init", "--bare", str(remote)], check=True, capture_output=True)
    subprocess.run(["git", "init", "-b", "jazzy", str(repo)], check=True, capture_output=True)
    git(repo, "config", "user.name", "Test")
    git(repo, "config", "user.email", "test@example.invalid")
    (repo / "file.txt").write_text("base\n", encoding="utf-8")
    git(repo, "add", "file.txt")
    git(repo, "commit", "-m", "base")
    git(repo, "remote", "add", "origin", str(remote))
    git(repo, "push", "-u", "origin", "jazzy")
    monkeypatch.setattr(MODULE, "ROOT", root)
    return root, repo


def test_published_head_is_safe_to_pin(published_repo):
    _, repo = published_repo
    assert MODULE.pinned_revision("org/repo", repo) == git(repo, "rev-parse", "HEAD")


def test_unpublished_head_pins_fetchable_series_base(published_repo):
    root, repo = published_repo
    base = git(repo, "rev-parse", "HEAD")
    (repo / "file.txt").write_text("local\n", encoding="utf-8")
    git(repo, "commit", "-am", "local")
    (root / "patches" / "org__repo.patch").write_text("placeholder\n", encoding="utf-8")
    (root / "patches" / "org__repo.base").write_text(base + "\n", encoding="ascii")
    assert MODULE.pinned_revision("org/repo", repo) == base


def test_unpublished_head_without_fallback_fails_closed(published_repo):
    _, repo = published_repo
    (repo / "file.txt").write_text("local\n", encoding="utf-8")
    git(repo, "commit", "-am", "local")
    with pytest.raises(ValueError, match="unpublished.*no fallback patch"):
        MODULE.pinned_revision("org/repo", repo)


def test_series_base_must_still_be_contained_in_a_fetchable_ref(published_repo):
    root, repo = published_repo
    (repo / "orphan.txt").write_text("orphan\n", encoding="utf-8")
    git(repo, "add", "orphan.txt")
    git(repo, "commit", "-m", "unpublished orphan base")
    orphan = git(repo, "rev-parse", "HEAD")
    (root / "patches" / "org__repo.patch").write_text(
        "placeholder\n", encoding="utf-8"
    )
    (root / "patches" / "org__repo.base").write_text(
        orphan + "\n", encoding="ascii"
    )
    with pytest.raises(ValueError, match="not contained in an origin ref/tag"):
        MODULE.pinned_revision("org/repo", repo)
