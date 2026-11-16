# tree-sitter-console

A tolerant Tree-sitter grammar for console transcripts.

The parser is intentionally small: it separates shell input from command output.
Lines beginning with `$` start Bash commands, and continuation lines stay part of
the command while common Bash syntax remains incomplete. Everything else is
console output.

## Development

Install Node.js, npm, Make, and a C compiler. CI uses Node.js 24.
Install the pinned Tree-sitter CLI locally, then run the full validation:

```sh
npm ci
make tests
```

`make tests` regenerates the parser, runs the corpus tests, parses the example
transcript, and checks both query files using the local CLI. Its caches default
to `.cache/`; `XDG_CACHE_HOME` and `TREE_SITTER_CACHE_DIR` can override them.

For individual steps, use `npm run generate`, `npm test`, or
`npm run parse -- examples/session.console`.

Edit `grammar.js` and `src/scanner.c` for parser changes, add focused examples in
`test/corpus/`, and commit regenerated files under `src/`. CI checks that the
generated artifacts are up to date. Read [ARCHITECTURE.md](ARCHITECTURE.md) for
the parser strategy and [HIGHLIGHTING.md](HIGHLIGHTING.md) before changing query
captures or the named nodes they use.

Neovim and other Tree-sitter clients can use `queries/injections.scm` to inject
Bash into `command_text` and `continuation_text` nodes.
