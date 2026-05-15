# Architecture

This parser is editor-first. Its job is not to parse Bash; it only identifies
which physical lines are shell input so a Bash parser can be injected into those
ranges.

The grammar has three visible line kinds:

- `command`: a line beginning with a `$` prompt, plus any Bash continuation lines
- `continuation`: a physical line that belongs to the previous command
- `output`: all other nonblank lines

Continuation detection lives in `src/scanner.c`, because it needs state across
lines. The scanner is organized around small helpers so new continuation cases
can be added without changing the grammar shape.

Supported continuation families:

- trailing backslash-newline
- open single, double, ANSI-C, and backtick quotes
- open `(`, `{`, and `[[` delimiters
- compound Bash keywords such as `if`, `for`, `while`, `case`, `select`, and
  `until`
- trailing control operators such as `|`, `&&`, and `||`
- heredocs with `<<` and `<<-`

The scanner deliberately keeps this heuristic and recoverable. If a line is not
clearly part of an unfinished Bash command, the parser treats it as output.
