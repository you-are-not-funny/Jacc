# Contributing

Send patches as pull requests or by email. Keep them small and focused.

## Sign-off

Every commit needs a `Signed-off-by:` line (`git commit -s`). By adding
it you certify the Developer Certificate of Origin, the same one the
Linux kernel uses (https://developercertificate.org): you wrote the
change or have the right to submit it, and you're okay with it being
released under this project's license.

## Licensing of contributions

- Source files carry an SPDX line at the top. Use `GPL-2.0-only`, or
  `GPL-2.0-only WITH Jacc-runtime-exception` for files under `runtime/`.
- No code under a license that conflicts with GPL-2.0-only.
- Copyright stays with whoever wrote it. There's no copyright
  assignment. Nobody can relicense the project without everybody's say.

## Style

Kernel-ish C: tabs, 80 columns, `snake_case`. C++ in `passes/` follows
the same layout. Comment why, not what.

## Tests

`make check` must pass. A change to a pass needs a case in
`tests/loops.c` showing what now gets removed or kept. A check-removing
change also needs one showing a case that must *keep* its check, because
the dangerous bug here is a check that's removed when it shouldn't be.
