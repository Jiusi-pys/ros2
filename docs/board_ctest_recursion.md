# Recursive CTest board plans

The board driver generator now follows generated `subdirs(...)` declarations
within the selected package's build directory. This is necessary for rclcpp:
its top-level CTest file registers linters, while native tests are registered
under `test/rclcpp`. Reading only the top-level file omitted those tests and
made an exact native selector appear absent.

The parser reads regular UTF-8 CTest files with depth, file-count and byte
bounds. Relative parent references such as `../../gtest` are accepted only
while every resolved directory remains inside the package. Missing files,
symlinks, repeated directories, path escapes and duplicate test names fail
before a driver is emitted. Duplicate names cannot share a board log/XML
artifact silently.

Fifteen parser tests pass. New RED cases covered nested selection and complete
plans, missing descendants, path escape, duplicate names, and a valid relative
parent traversal; each became GREEN after implementation. Actual generated
plans for the recorded rclcpp snapshot contain 136 entries. The exact
`test_signal_chaining` selector emits one native entry from its nested path.
These are plan counts, not runtime pass counts.

```powershell
.pixi/envs/default/python.exe -m pytest scripts/test_parse_ctest_env.py -q
```

Run `signal_chain_red_20260907` stopped before board-driver staging because
the old parser could not resolve the nested selector. No native verdict was
produced. Its retained activity lock was released only after confirming no
run-scoped ready/terminal inputs existed and matching the exact nonce/owner.
Subsequent native RED/GREEN results are recorded separately from this
infrastructure correction.
