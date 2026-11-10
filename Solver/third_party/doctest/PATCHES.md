# Local changes to doctest 2.4.12

`doctest.h` is vendored (decision D5). When it is updated to a new version, re-apply or drop these changes.

| Line (2.4.12) | Change | Why |
| :--- | :--- | :--- |
| 3562 | `Approx(...).epsilon(DBL_EPSILON) != 0` → `!= 0.0` | Added in M3 (commit 2e5144f/452a9cc) for a build with AppleClang 21. With g++ 16.2.0 (MSYS2) the original line builds without warnings, so the change only matters for Clang. Behaviour is identical. |
