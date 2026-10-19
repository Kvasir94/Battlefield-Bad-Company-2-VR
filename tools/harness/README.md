# BC2 test-loop harness

Runners, batch scripts and report viewers for scripted in-game rows on a
development PC. By default the scripts use the checkout that contains them
(`..\..` from this folder) and the test-range stage in `$env:FVR_BC2_STAGE`.
When that variable is unset, they use `%USERPROFILE%\fvr-testrange-stage`.
Pass `-Repo`/`-Root` and `-Stage` to override either one.

`tools\harness\build-testrange.ps1` builds and fills the stage. See
[docs/BC2_TEST_RANGE.md](../../docs/BC2_TEST_RANGE.md) for the test-range DLL,
its config files and what it reports.
