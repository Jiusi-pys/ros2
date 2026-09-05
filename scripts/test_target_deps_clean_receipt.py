from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "target_deps_src" / "lib"))

import clean_deps_receipt as receipt  # noqa: E402


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class CleanDepsReceiptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary.name).resolve()
        self.prefix = self.workspace / "install_ohos"
        self.evidence = self.workspace / "evidence"
        self.begin_path = self.evidence / "begin.json"
        self.journal_path = self.evidence / "journal.jsonl"
        self.manifest_path = self.prefix / receipt.MANIFEST_NAME
        self.receipt_path = self.prefix / receipt.RECEIPT_NAME
        self.sdk = self.workspace / "sdk" / "native"
        self.python_target = self.workspace / "python_target" / "usr"
        self.python_sitepkgs = self.workspace / "python_target" / "sitepkgs"
        self.source_payloads = {
            "alpha-1.0.tar.gz": b"alpha source archive\n",
            "beta-2.0.tar.xz": b"beta source archive\n",
        }
        self.source_paths = {
            name: self.workspace / "target_deps_src" / name
            for name in self.source_payloads
        }
        self.patch_path = self.workspace / "target_deps_src" / "alpha-ohos.patch"
        self.patch_target = self.workspace / "target_deps_src" / "alpha-1.0"
        self._create_workspace()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write(self, relative: str, payload: str | bytes = "input\n") -> Path:
        path = self.workspace / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(payload, bytes):
            path.write_bytes(payload)
        else:
            path.write_text(payload, encoding="utf-8")
        return path

    def _git(self, *arguments: str) -> None:
        completed = subprocess.run(
            ["git", "-C", str(self.workspace), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode != 0:
            self.fail(completed.stderr.decode("utf-8", "replace"))

    def _create_workspace(self) -> None:
        self.evidence.mkdir(parents=True)
        self._write(
            ".gitignore",
            """target_deps_src/*
!target_deps_src/*.sh
!target_deps_src/*.patch
!target_deps_src/sources.lock
!target_deps_src/lib/
target_deps_src/lib/*
!target_deps_src/lib/*.sh
!target_deps_src/lib/*.py
""",
        )
        self._write("pixi.toml", "[workspace]\nname='test'\n")
        self._write("pixi.lock", "version: 1\n")
        self._write("cmake/ohos-aarch64.toolchain.cmake", "set(TEST_TOOLCHAIN ON)\n")
        self._write("scripts/build_target_deps.sh", "#!/usr/bin/env bash\n")
        self._write("scripts/python_target.py", "# target Python verifier\n")
        self._write("scripts/python/ohos_python.lock.json", "{}\n")
        self._write("target_deps_src/build_all_clean_ohos.sh", "#!/usr/bin/env bash\n")
        self._write("target_deps_src/lib/clean_deps_receipt.py", "# receipt input\n")
        self._write("target_deps_src/lib/locked_sources.sh", "# source helper input\n")
        self._write("target_deps_src/alpha-ohos.patch", "--- a/a\n+++ b/a\n")
        lock_lines = ["# kind name digest url"]
        for name, payload in self.source_payloads.items():
            lock_lines.append(
                f"archive {name} {digest(payload)} https://example.invalid/{name}"
            )
        self._write("target_deps_src/sources.lock", "\n".join(lock_lines) + "\n")
        self._write(".pixi/envs/default/python.exe", b"python test tool\n")
        self._write(".pixi/envs/default/Library/bin/cmake.exe", b"cmake test tool\n")
        self._write(".pixi/envs/default/Library/bin/ninja.exe", b"ninja test tool\n")
        self._write(".pixi/envs/default/Library/bin/make.exe", b"make test tool\n")
        self._write(".pixi/envs/default/Library/bin/curl.exe", b"curl test tool\n")
        self._write(".pixi/envs/default/Library/bin/git.exe", b"git shim test tool\n")
        self._write(".pixi/envs/default/Library/mingw64/bin/git.exe", b"git test tool\n")
        self._write(".pixi/envs/default/Library/usr/bin/tar.exe", b"tar test tool\n")
        self._write(".pixi/envs/default/Library/usr/bin/patch.exe", b"patch test tool\n")
        self._write(
            ".pixi/envs/default/Library/usr/bin/sha256sum.exe", b"sha256sum test tool\n"
        )
        self._write("host/git/usr/bin/sh.exe", b"git bash sh test tool\n")
        self._write("host/git/usr/bin/bash.exe", b"git bash test tool\n")
        self._write("host/git/usr/bin/which.exe", b"git which test tool\n")
        self._write(".pixi/envs/default/Scripts/sip-build.exe", b"sip-build frontend\n")
        self._write(
            ".pixi/envs/default/Lib/site-packages/sipbuild/__init__.py",
            "# sip frontend\n",
        )
        self._write(
            ".pixi/envs/default/Lib/site-packages/sip-6.8.6.dist-info/METADATA",
            "Version: 6.8.6\n",
        )
        self._write(
            ".pixi/envs/default/Lib/site-packages/pyqtbuild/__init__.py",
            "# pyqt frontend\n",
        )
        self._write(
            ".pixi/envs/default/Lib/site-packages/pyqt_builder-1.19.1.dist-info/METADATA",
            "Version: 1.19.1\n",
        )
        self._write("sdk/native/llvm/bin/clang.exe", b"target clang\n")
        self._write("sdk/native/sysroot/usr/include/stdio.h", b"/* target */\n")
        self._write("python_target/usr/include/python3.12/Python.h", b"/* python */\n")
        self._write("python_target/usr/lib/libpython3.12.so", b"target python elf\n")
        self._write("python_target/sitepkgs/.ros2-ohos-python-stage.json", "{}\n")
        self._write("python_target/sitepkgs/yaml/__init__.py", "safe_load = None\n")
        self._git("init", "--quiet")
        self._git("add", ".gitignore", "pixi.toml", "pixi.lock", "cmake", "scripts", "target_deps_src")

    def _run(self, arguments: list[str]) -> tuple[int, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = receipt.main(arguments)
        return result, stdout.getvalue() + stderr.getvalue()

    def _begin(self) -> tuple[int, str]:
        resolved = {
            "sh": self.workspace / "host/git/usr/bin/sh.exe",
            "bash": self.workspace / "host/git/usr/bin/bash.exe",
            "tar": self.workspace / ".pixi/envs/default/Library/usr/bin/tar.exe",
            "patch": self.workspace / ".pixi/envs/default/Library/usr/bin/patch.exe",
            "which": self.workspace / "host/git/usr/bin/which.exe",
            "sha256sum": self.workspace
            / ".pixi/envs/default/Library/usr/bin/sha256sum.exe",
            "make": self.workspace / ".pixi/envs/default/Library/bin/make.exe",
            "cmake": self.workspace / ".pixi/envs/default/Library/bin/cmake.exe",
            "ninja": self.workspace / ".pixi/envs/default/Library/bin/ninja.exe",
            "curl": self.workspace / ".pixi/envs/default/Library/bin/curl.exe",
            "git": self.workspace / ".pixi/envs/default/Library/bin/git.exe",
        }
        with mock.patch.object(
            receipt.shutil, "which", side_effect=lambda name: str(resolved[name])
        ):
            return self._run(
                [
                    "begin",
                    "--workspace",
                    str(self.workspace),
                    "--prefix",
                    str(self.prefix),
                    "--sdk-root",
                    str(self.sdk),
                    "--python-target-root",
                    str(self.python_target),
                    "--python-sitepkgs-root",
                    str(self.python_sitepkgs),
                    "--journal",
                    str(self.journal_path),
                    "--expected-recipe",
                    *sorted(receipt.EXPECTED_RECIPES),
                    "--output",
                    str(self.begin_path),
                ]
            )

    def _journal(self, *arguments: str) -> tuple[int, str]:
        return self._run(["journal", "--journal", str(self.journal_path), *arguments])

    def _materialize_sources(self) -> None:
        for name, payload in self.source_payloads.items():
            self.source_paths[name].write_bytes(payload)
        self.patch_target.mkdir()

    def _record_sources(self, *, omit: str | None = None) -> None:
        for name, payload in self.source_payloads.items():
            if name == omit:
                continue
            code, output = self._journal(
                "--event",
                "source_verified",
                "--name",
                name,
                "--kind",
                "archive",
                "--digest",
                digest(payload),
                "--url",
                f"https://example.invalid/{name}",
                "--path",
                str(self.source_paths[name]),
            )
            self.assertEqual(0, code, output)

    def _record_patch_and_recipes(self) -> None:
        code, output = self._journal(
            "--event",
            "patch_applied",
            "--name",
            self.patch_path.name,
            "--digest",
            digest(self.patch_path.read_bytes()),
            "--path",
            str(self.patch_path),
            "--target",
            str(self.patch_target),
        )
        self.assertEqual(0, code, output)
        for name in sorted(receipt.EXPECTED_RECIPES):
            code, output = self._journal(
                "--event", "recipe_complete", "--name", name
            )
            self.assertEqual(0, code, output)

    def _materialize_prefix(self) -> None:
        (self.prefix / "lib").mkdir(parents=True)
        (self.prefix / "include" / "alpha").mkdir(parents=True)
        (self.prefix / "lib" / "libalpha.so").write_bytes(b"target elf\n")
        (self.prefix / "include" / "alpha" / "alpha.h").write_text(
            "#define ALPHA 1\n", encoding="utf-8"
        )

    def _finish(self) -> tuple[int, str]:
        return self._run(
            [
                "finish",
                "--workspace",
                str(self.workspace),
                "--begin",
                str(self.begin_path),
                "--journal",
                str(self.journal_path),
                "--prefix",
                str(self.prefix),
                "--manifest",
                str(self.manifest_path),
                "--output",
                str(self.receipt_path),
            ]
        )

    def _verify(self) -> tuple[int, str]:
        return self._run(
            [
                "verify",
                "--workspace",
                str(self.workspace),
                "--prefix",
                str(self.prefix),
                "--manifest",
                str(self.manifest_path),
                "--receipt",
                str(self.receipt_path),
            ]
        )

    def _complete(self) -> None:
        code, output = self._begin()
        self.assertEqual(0, code, output)
        self._materialize_sources()
        self._record_sources()
        self._record_patch_and_recipes()
        self._materialize_prefix()
        code, output = self._finish()
        self.assertEqual(0, code, output)

    def test_begin_rejects_preexisting_prefix(self) -> None:
        self._materialize_prefix()
        code, output = self._begin()
        self.assertEqual(1, code)
        self.assertIn("prefix path to be absent", output)
        self.assertFalse(self.begin_path.exists())

    def test_begin_rejects_ignored_target_deps_cache(self) -> None:
        self._write("target_deps_src/cache/archive.part", b"stale\n")
        code, output = self._begin()
        self.assertEqual(1, code)
        self.assertIn("Git-ignored", output)
        self.assertFalse(self.begin_path.exists())

    def test_finish_rejects_missing_locked_source_journal_event(self) -> None:
        code, output = self._begin()
        self.assertEqual(0, code, output)
        self._materialize_sources()
        self._record_sources(omit="beta-2.0.tar.xz")
        self._record_patch_and_recipes()
        self._materialize_prefix()
        code, output = self._finish()
        self.assertEqual(1, code)
        self.assertIn("lacks verified source events", output)
        self.assertFalse(self.manifest_path.exists())
        self.assertFalse(self.receipt_path.exists())

    def test_finish_rejects_input_changed_after_begin(self) -> None:
        code, output = self._begin()
        self.assertEqual(0, code, output)
        self._write("cmake/ohos-aarch64.toolchain.cmake", "set(CHANGED ON)\n")
        self._materialize_sources()
        self._record_sources()
        self._record_patch_and_recipes()
        self._materialize_prefix()
        code, output = self._finish()
        self.assertEqual(1, code)
        self.assertIn("input changed", output)

    def test_verify_rejects_extra_and_modified_prefix_files(self) -> None:
        self._complete()
        code, output = self._verify()
        self.assertEqual(0, code, output)

        extra = self.prefix / "share" / "stale.txt"
        extra.parent.mkdir()
        extra.write_text("stale\n", encoding="utf-8")
        code, output = self._verify()
        self.assertEqual(1, code)
        self.assertIn("exact inventory differs", output)

        shutil.rmtree(extra.parent)
        library = self.prefix / "lib" / "libalpha.so"
        library.write_bytes(b"tampered elf\n")
        code, output = self._verify()
        self.assertEqual(1, code)
        self.assertIn("exact inventory differs", output)

    def test_verify_rejects_receipt_self_hash_tampering(self) -> None:
        self._complete()
        record = json.loads(self.receipt_path.read_text(encoding="utf-8"))
        record["result"] = "FAIL"
        self.receipt_path.write_text(json.dumps(record), encoding="utf-8")
        code, output = self._verify()
        self.assertEqual(1, code)
        self.assertIn("completed clean dependency PASS", output)

    def test_verify_rejects_executed_host_frontend_change(self) -> None:
        self._complete()
        self._write(
            ".pixi/envs/default/Lib/site-packages/sipbuild/__init__.py",
            "# tampered after build\n",
        )
        code, output = self._verify()
        self.assertEqual(1, code)
        self.assertIn("host frontend changed", output)

    def test_verify_rejects_actual_host_tool_change(self) -> None:
        self._complete()
        self._write("host/git/usr/bin/sh.exe", b"tampered Git Bash sh\n")
        code, output = self._verify()
        self.assertEqual(1, code)
        self.assertIn("host build tool changed", output)

    def test_relative_symlink_validation_rejects_absolute_and_escape(self) -> None:
        receipt._validate_relative_symlink("lib/current", "../libalpha.so")
        with self.assertRaises(receipt.ReceiptError):
            receipt._validate_relative_symlink("lib/current", "../../outside")
        with self.assertRaises(receipt.ReceiptError):
            receipt._validate_relative_symlink("lib/current", "C:/outside")
        with self.assertRaises(receipt.ReceiptError):
            receipt._validate_relative_symlink("lib/current", "/outside")

    def test_inventory_supports_relative_symlink_when_host_allows_it(self) -> None:
        root = self.workspace / "links"
        (root / "lib").mkdir(parents=True)
        (root / "lib" / "actual.so").write_bytes(b"elf\n")
        try:
            os.symlink("actual.so", root / "lib" / "current.so")
        except OSError as exc:
            self.skipTest(f"host cannot create a test symlink: {exc}")
        entries = receipt.tree_inventory(root)
        link = next(entry for entry in entries if entry["path"] == "lib/current.so")
        self.assertEqual("symlink", link["type"])
        self.assertEqual("actual.so", link["link"])


if __name__ == "__main__":
    unittest.main()
