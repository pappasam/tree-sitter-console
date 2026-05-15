TREE_SITTER_CONFIG := tree-sitter.config.json
EXAMPLE_CONSOLE := examples/session.console

.PHONY: tests
tests:
	npm run generate
	npm test
	tree-sitter parse --quiet --config-path $(TREE_SITTER_CONFIG) $(EXAMPLE_CONSOLE)
	tree-sitter query --config-path $(TREE_SITTER_CONFIG) queries/highlights.scm $(EXAMPLE_CONSOLE) >/dev/null
	tree-sitter query --config-path $(TREE_SITTER_CONFIG) queries/injections.scm $(EXAMPLE_CONSOLE) >/dev/null
