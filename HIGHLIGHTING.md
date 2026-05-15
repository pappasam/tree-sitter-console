# Highlighting

This grammar does not directly highlight Bash syntax. It exposes command ranges
for a Bash injection.

## Capture Contract

- `@punctuation.special`: the `$` prompt
- `@text.literal`: console output, if a client wants to style it

The Bash injection captures `command_text` and `continuation_text`, excluding the
leading prompt from injected content.
