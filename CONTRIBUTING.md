# Contributing

Thank you for helping. This repository is the companion to a book, which shapes what can change and
how. Please read this page before opening a pull request.

## What is welcome

- **Bug reports and fixes** in the viewer, the harness, the scripts or the plugins.
- **Measurements from other machines.** The book's numbers come from one laptop. A sweep from a
  different CPU, GPU, display or operating system, with its reports, is valuable, especially if the
  ratios between designs come out differently.
- **Ports and builds** for platforms not yet tried (Windows, macOS, other browsers).
- **Clarifications** in the step notes, the scripts' output or the README.

Ideas for new features are welcome as issues first. The program exists to teach, so a feature is worth
adding when it puts a new kind of pressure on the design.

## The tags never move

`experiments-complete` and `step-01` to `step-11` are fixed points: the book's listings name files and
line numbers at those tags. They are never rewritten or moved, and no pull request should target them.

All changes land on `main`. If a fix concerns the code of a step, say in the pull request which steps
and chapters it affects; it may be listed as an erratum for the book.

## Building and checking

See the [README](README.md) for what you need. Before opening a pull request, run:

```bash
./scripts/check.sh
```

It builds the release preset, runs every test, and runs `qmllint` and `qmlformat` over the QML. If Qt is
not in `/opt/Qt/6.12.0/gcc_64`, set `QT_DIR` to its `gcc_64` directory first. A pull request should
leave it passing.

## Measurements

A change that claims to be faster, smaller or more stable needs a measurement, made the way the book
makes them:

- Use the `release` preset, never `debug`.
- Write down what you expect before you measure, and keep it in the pull request even if it was wrong.
- Run the relevant script from `scripts/` (each step's notes in `docs/steps/` name it), with its
  warm-up, and repeat runs on a busy machine.
- Commit the reports under `results/`, next to the summaries they produce, and say which machine they
  came from.

A number without its machine and its method cannot be compared with anything.

## Code

- C++20 and Qt 6.12. Follow the style of the file you are editing.
- QML must pass `qmllint` and be formatted with `qmlformat`.
- Comments are few and say why, not what.
- Nothing slow runs on the GUI thread, and nothing grows without a bound. Those two rules are most of
  the book.
- New behaviour comes with a test where one is possible (`common/tests/`, `app/tests/`).

## Commits and pull requests

- One logical change per commit, with a message of one sentence in the imperative.
- Keep pull requests small enough to review in one sitting; describe what changed, why, and how it was
  checked or measured.
- By contributing you agree that your contribution is licensed under the [MIT licence](LICENSE).

## Conduct

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).
