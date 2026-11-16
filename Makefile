TREE_SITTER := ./node_modules/.bin/tree-sitter
TREE_SITTER_CONFIG := tree-sitter.config.json
EXAMPLE_CONSOLE := examples/session.console

export XDG_CACHE_HOME ?= $(CURDIR)/.cache
export TREE_SITTER_CACHE_DIR ?= $(XDG_CACHE_HOME)/tree-sitter

.PHONY: tests
tests:
	$(TREE_SITTER) generate
	$(TREE_SITTER) test --config-path $(TREE_SITTER_CONFIG)
	$(TREE_SITTER) parse --quiet --config-path $(TREE_SITTER_CONFIG) $(EXAMPLE_CONSOLE)
	$(TREE_SITTER) query --config-path $(TREE_SITTER_CONFIG) queries/highlights.scm $(EXAMPLE_CONSOLE) >/dev/null
	$(TREE_SITTER) query --config-path $(TREE_SITTER_CONFIG) queries/injections.scm $(EXAMPLE_CONSOLE) >/dev/null
