import subprocess
import sys
from pathlib import Path


SCRIPT = Path(__file__).with_name("_parse_ctest_env.py")


def test_recursive_subdirectory_selector_and_complete_plan(tmp_path):
    nested = tmp_path / "test" / "rclcpp"
    nested.mkdir(parents=True)
    (tmp_path / "test" / "CTestTestfile.cmake").write_text('subdirs("rclcpp")\n')
    (nested / "CTestTestfile.cmake").write_text(wrapped_test(
        "test_signal_chaining", "C:/workspace/ros2/build_ohos/demo_pkg/test/rclcpp/test_signal_chaining"))
    root = wrapped_test("top", "C:/workspace/ros2/build_ohos/demo_pkg/top") + 'subdirs("test")\n'
    selected = run_parser(tmp_path, root, "--only-test", "test_signal_chaining")
    assert selected.returncode == 0, selected.stderr
    assert selected.stdout.count("# BOARDTEST_EXPECTED ") == 1
    assert "./test/rclcpp/test_signal_chaining" in selected.stdout
    full = run_parser(tmp_path, root)
    assert full.returncode == 0, full.stderr
    assert full.stdout.count("# BOARDTEST_EXPECTED ") == 2


def test_missing_nested_ctest_file_is_not_silently_ignored(tmp_path):
    result = run_parser(tmp_path, 'subdirs("missing")\n')
    assert result.returncode == 2
    assert not result.stdout


def test_nested_ctest_cannot_escape_package(tmp_path):
    result = run_parser(tmp_path, 'subdirs("../outside")\n')
    assert result.returncode == 2
    assert not result.stdout


def test_nested_relative_parent_stays_within_package(tmp_path):
    nested = tmp_path / "test" / "rclcpp"
    nested.mkdir(parents=True)
    (tmp_path / "test" / "CTestTestfile.cmake").write_text('subdirs("rclcpp")\n')
    (nested / "CTestTestfile.cmake").write_text('subdirs("../../gtest")\n')
    (tmp_path / "gtest").mkdir()
    (tmp_path / "gtest" / "CTestTestfile.cmake").write_text(wrapped_test(
        "helper", "C:/workspace/ros2/build_ohos/demo_pkg/gtest/helper"))
    result = run_parser(tmp_path, 'subdirs("test")\n', "--only-test", "helper")
    assert result.returncode == 0, result.stderr
    assert "# BOARDTEST_EXPECTED helper" in result.stdout


def test_duplicate_nested_test_names_are_rejected(tmp_path):
    nested = tmp_path / "child"
    nested.mkdir()
    case = wrapped_test("duplicate", "C:/workspace/ros2/build_ohos/demo_pkg/exe")
    (nested / "CTestTestfile.cmake").write_text(case)
    result = run_parser(tmp_path, case + 'subdirs("child")\n')
    assert result.returncode == 2
    assert not result.stdout


def run_parser(tmp_path: Path, ctest_text: str, *extra: str):
    ctest_file = tmp_path / "CTestTestfile.cmake"
    ctest_file.write_text(ctest_text, encoding="utf-8", newline="\n")
    return subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            str(ctest_file),
            "demo_pkg",
            "C:/workspace/ros2",
            *extra,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def wrapped_test(name: str, executable: str, *arguments: str) -> str:
    args = " ".join(f'"{argument}"' for argument in arguments)
    return (
        f'add_test([=[{name}]=] "C:/host/python.exe" "-u" '
        f'"C:/host/run_test.py" "--command" "{executable}" {args})\n'
    )


def wrapped_test_with_env(
    name: str, executable: str, environment: str, *arguments: str
) -> str:
    args = " ".join(f'"{argument}"' for argument in arguments)
    return (
        f'add_test([=[{name}]=] "C:/host/python.exe" "-u" '
        f'"C:/host/run_test.py" "--env" "{environment}" '
        f'"--command" "{executable}" {args})\n'
    )


def test_gtest_xml_uri_is_remapped_without_corrupting_xml_prefix(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test(
            "test_transport",
            "C:/workspace/ros2/build_ohos/demo_pkg/test_transport",
            "--gtest_output=xml:C:/workspace/ros2/build_ohos/demo_pkg/test_results/test_transport.gtest.xml",
        ),
    )
    assert result.returncode == 0, result.stderr
    expected = (
        "--gtest_output=xml:/data/local/tmp/ros2/tests/"
        "demo_pkg/test_transport.xml"
    )
    assert expected in result.stdout
    assert "# BOARDTEST_XML test_transport" in result.stdout
    assert "--gtest_output=xm:" not in result.stdout
    assert "C:/workspace" not in result.stdout


def test_driver_has_complete_plan_archived_verdicts_and_cumulative_exit(tmp_path):
    ctest = wrapped_test(
        "passes_first",
        "C:/workspace/ros2/build_ohos/demo_pkg/passes_first",
    ) + wrapped_test(
        "fails_second",
        "C:/workspace/ros2/build_ohos/demo_pkg/fails_second",
    )
    result = run_parser(tmp_path, ctest)
    assert result.returncode == 0, result.stderr
    guarded_source = ". /data/local/tmp/ros2/env.sh || exit 70"
    assert guarded_source in result.stdout
    assert result.stdout.index(guarded_source) < result.stdout.index(
        'MDDS_TOKEN_EXEC="${MDDS_TOKEN_EXEC:-'
    )
    assert ". /data/local/tmp/ros2/env.sh\n" not in result.stdout
    assert result.stdout.count("# BOARDTEST_EXPECTED ") == 2
    assert result.stdout.count("# BOARDTEST_TOKEN_MODE ") == 2
    assert "overall_rc=0" in result.stdout
    assert "overall_rc=1" in result.stdout
    assert ".boardtest-verdicts/passes_first" in result.stdout
    assert ".boardtest-verdicts/fails_second" in result.stdout
    assert result.stdout.rstrip().endswith('exit "$overall_rc"')


def test_skip_is_planned_and_has_an_archived_canonical_record(tmp_path):
    ctest = wrapped_test(
        "lint_only",
        "C:/workspace/ros2/build_ohos/demo_pkg/lint_only",
        "--skip-test",
    )
    result = run_parser(tmp_path, ctest)
    assert result.returncode == 0, result.stderr
    assert "# BOARDTEST_EXPECTED lint_only" in result.stdout
    assert "line='BOARDTEST lint_only SKIP'" in result.stdout
    assert ".boardtest-verdicts/lint_only" in result.stdout


def test_absent_exact_selector_fails_closed(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test(
            "present",
            "C:/workspace/ros2/build_ohos/demo_pkg/present",
        ),
        "--only-test",
        "missing",
    )
    assert result.returncode == 2
    assert "requested CTest selector is absent" in result.stderr


def test_unsafe_ctest_name_fails_instead_of_creating_ambiguous_evidence(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test(
            "bad name",
            "C:/workspace/ros2/build_ohos/demo_pkg/test_bad",
        ),
    )
    assert result.returncode == 2
    assert "unsafe CTest name" in result.stderr


def test_token_boundary_modes_are_explicit_and_not_name_coupled(tmp_path):
    probe = (
        "C:/workspace/ros2/build_ohos/demo_pkg/mdds_token_boundary_probe"
    )
    ctest = wrapped_test_with_env(
        "arbitrary_negative_registration",
        probe,
        "MDDS_BOARDTEST_TOKEN_MODE=BYPASS_EXPECT_UNAUTHORIZED",
        "failure",
        "93",
    ) + wrapped_test_with_env(
        "arbitrary_positive_registration",
        "C:/workspace/ros2/build_ohos/demo_pkg/mdds_token_exec",
        "MDDS_BOARDTEST_TOKEN_MODE=LAUNCHER_UNDER_TEST",
        "--",
        probe,
        "success",
        "93",
    )
    result = run_parser(tmp_path, ctest)
    assert result.returncode == 0, result.stderr
    assert (
        "# BOARDTEST_TOKEN_MODE arbitrary_negative_registration "
        "BYPASS_EXPECT_UNAUTHORIZED"
    ) in result.stdout
    assert (
        "# BOARDTEST_TOKEN_MODE arbitrary_positive_registration "
        "LAUNCHER_UNDER_TEST"
    ) in result.stdout
    negative = next(
        line for line in result.stdout.splitlines()
        if "./mdds_token_boundary_probe failure 93 "
        "> arbitrary_negative_registration.log" in line
    )
    positive = next(
        line for line in result.stdout.splitlines()
        if "./mdds_token_exec -- ./mdds_token_boundary_probe success 93 "
        "> arbitrary_positive_registration.log" in line
    )
    assert '"$MDDS_TOKEN_EXEC" --' not in negative
    assert '"$MDDS_TOKEN_EXEC" --' not in positive
    assert "MDDS_BOARDTEST_TOKEN_MODE=" not in negative
    assert "MDDS_BOARDTEST_TOKEN_MODE=" not in positive
    assert result.stdout.index(negative) < result.stdout.index(positive)


def test_unknown_token_mode_fails_closed(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test_with_env(
            "probe",
            "C:/workspace/ros2/build_ohos/demo_pkg/token_probe",
            "MDDS_BOARDTEST_TOKEN_MODE=UNSAFE",
            "failure",
        ),
    )
    assert result.returncode == 2
    assert "invalid/duplicate MDDS_BOARDTEST_TOKEN_MODE" in result.stderr


def test_bypass_marker_cannot_be_moved_to_an_arbitrary_executable(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test_with_env(
            "renamed_test_is_not_the_contract",
            "C:/workspace/ros2/build_ohos/demo_pkg/always_returns_zero",
            "MDDS_BOARDTEST_TOKEN_MODE=BYPASS_EXPECT_UNAUTHORIZED",
            "failure",
            "93",
        ),
    )
    assert result.returncode == 2
    assert "reserved for the exact unprivileged token-boundary probe" in result.stderr


def test_launcher_under_test_is_direct_and_maps_built_probe_exactly(tmp_path):
    launcher = "C:/workspace/ros2/build_ohos/demo_pkg/mdds_token_exec"
    probe = "C:/workspace/ros2/build_ohos/demo_pkg/mdds_token_boundary_probe"
    result = run_parser(
        tmp_path,
        wrapped_test_with_env(
            "privileged_boundary",
            launcher,
            "MDDS_BOARDTEST_TOKEN_MODE=LAUNCHER_UNDER_TEST",
            "--",
            probe,
            "success",
            "93",
        ),
    )
    assert result.returncode == 0, result.stderr
    assert (
        "# BOARDTEST_TOKEN_MODE privileged_boundary LAUNCHER_UNDER_TEST"
    ) in result.stdout
    command = next(
        line for line in result.stdout.splitlines()
        if " > privileged_boundary.log 2>&1" in line
    )
    assert (
        "timeout 180 ./mdds_token_exec -- ./mdds_token_boundary_probe success 93"
    ) in command
    assert '"$MDDS_TOKEN_EXEC" --' not in command
    assert "C:/workspace" not in command


def test_launcher_under_test_rejects_unsafe_build_relative_argument(tmp_path):
    result = run_parser(
        tmp_path,
        wrapped_test_with_env(
            "privileged_boundary",
            "C:/workspace/ros2/build_ohos/demo_pkg/mdds_token_exec",
            "MDDS_BOARDTEST_TOKEN_MODE=LAUNCHER_UNDER_TEST",
            "--",
            "C:/workspace/ros2/build_ohos/demo_pkg/../foreign_probe",
        ),
    )
    assert result.returncode == 2
    assert "requires the exact packaged launcher/probe contract" in result.stderr
