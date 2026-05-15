# tree-sitter-console

A tolerant Tree-sitter grammar for console transcripts.

The parser is intentionally small: it separates shell input from command output.
Lines beginning with `$` start Bash commands, and continuation lines stay part of
the command while common Bash syntax remains incomplete. Everything else is
console output.

## Development

```sh
npm run generate
npm test
```

Neovim and other Tree-sitter clients can use `queries/injections.scm` to inject
Bash into `command_text` and `continuation_text` nodes.
