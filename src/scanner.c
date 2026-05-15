#include "tree_sitter/parser.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum TokenType {
  COMMAND_PROMPT,
  COMMAND_TEXT,
  CONTINUATION_TEXT,
  OUTPUT_TEXT,
  NEWLINE,
  CONTINUATION_NEWLINE,
};

typedef enum {
  QUOTE_NONE,
  QUOTE_SINGLE,
  QUOTE_DOUBLE,
  QUOTE_ANSI_C,
  QUOTE_BACKTICK,
} Quote;

#define MAX_HEREDOCS 8
#define MAX_DELIMITER_LEN 64

typedef struct {
  bool at_line_start;
  Quote quote;
  uint16_t paren_depth;
  uint16_t brace_depth;
  uint16_t double_bracket_depth;
  uint16_t compound_depth;
  bool trailing_operator;
  bool trailing_backslash;
  uint8_t heredoc_count;
  char heredocs[MAX_HEREDOCS][MAX_DELIMITER_LEN];
  bool heredoc_strip_tabs[MAX_HEREDOCS];
} Scanner;

static void reset_command_state(Scanner *scanner) {
  scanner->quote = QUOTE_NONE;
  scanner->paren_depth = 0;
  scanner->brace_depth = 0;
  scanner->double_bracket_depth = 0;
  scanner->compound_depth = 0;
  scanner->trailing_operator = false;
  scanner->trailing_backslash = false;
  scanner->heredoc_count = 0;
}

static bool needs_more_bash(const Scanner *scanner) {
  return scanner->quote != QUOTE_NONE ||
         scanner->paren_depth > 0 ||
         scanner->brace_depth > 0 ||
         scanner->double_bracket_depth > 0 ||
         scanner->compound_depth > 0 ||
         scanner->trailing_operator ||
         scanner->trailing_backslash ||
         scanner->heredoc_count > 0;
}

static bool is_ident_start(char c) {
  return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

static bool word_equals(const char *word, size_t len, const char *expected) {
  return strlen(expected) == len && strncmp(word, expected, len) == 0;
}

static bool opens_compound(const char *word, size_t len) {
  return word_equals(word, len, "if") ||
         word_equals(word, len, "for") ||
         word_equals(word, len, "while") ||
         word_equals(word, len, "until") ||
         word_equals(word, len, "select") ||
         word_equals(word, len, "case");
}

static bool closes_compound(const char *word, size_t len) {
  return word_equals(word, len, "fi") ||
         word_equals(word, len, "done") ||
         word_equals(word, len, "esac");
}

static bool trails_incomplete_keyword(const char *word, size_t len) {
  return word_equals(word, len, "then") ||
         word_equals(word, len, "do") ||
         word_equals(word, len, "else") ||
         word_equals(word, len, "elif") ||
         word_equals(word, len, "case") ||
         word_equals(word, len, "in");
}

static void push_heredoc(Scanner *scanner, const char *delimiter, size_t len, bool strip_tabs) {
  if (scanner->heredoc_count >= MAX_HEREDOCS) return;
  if (len >= MAX_DELIMITER_LEN) len = MAX_DELIMITER_LEN - 1;
  memcpy(scanner->heredocs[scanner->heredoc_count], delimiter, len);
  scanner->heredocs[scanner->heredoc_count][len] = '\0';
  scanner->heredoc_strip_tabs[scanner->heredoc_count] = strip_tabs;
  scanner->heredoc_count++;
}

static bool line_matches_heredoc(const Scanner *scanner, const char *line, size_t len) {
  if (scanner->heredoc_count == 0) return false;

  const char *delimiter = scanner->heredocs[0];
  if (scanner->heredoc_strip_tabs[0]) {
    while (len > 0 && *line == '\t') {
      line++;
      len--;
    }
  }

  size_t delimiter_len = strlen(delimiter);
  return len == delimiter_len && strncmp(line, delimiter, delimiter_len) == 0;
}

static void pop_heredoc(Scanner *scanner) {
  if (scanner->heredoc_count == 0) return;
  for (uint8_t i = 1; i < scanner->heredoc_count; i++) {
    strcpy(scanner->heredocs[i - 1], scanner->heredocs[i]);
    scanner->heredoc_strip_tabs[i - 1] = scanner->heredoc_strip_tabs[i];
  }
  scanner->heredoc_count--;
}

static size_t trim_end(const char *line, size_t len) {
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
    len--;
  }
  return len;
}

static bool has_unescaped_trailing_backslash(const char *line, size_t len) {
  len = trim_end(line, len);
  if (len == 0 || line[len - 1] != '\\') return false;

  size_t slash_count = 0;
  while (len > 0 && line[len - 1] == '\\') {
    slash_count++;
    len--;
  }
  return slash_count % 2 == 1;
}

static bool trailing_control_operator(const char *line, size_t len, const char *last_word, size_t last_word_len) {
  len = trim_end(line, len);
  if (len == 0) return false;
  if (trails_incomplete_keyword(last_word, last_word_len)) return true;
  if (line[len - 1] == '|') return true;
  if (line[len - 1] == '&' && len >= 2 && line[len - 2] == '&') return true;
  if (line[len - 1] == '(' || line[len - 1] == '{') return true;
  return false;
}

static size_t shell_comment_start(const char *line, size_t len) {
  Quote quote = QUOTE_NONE;
  bool escaped = false;

  for (size_t i = 0; i < len; i++) {
    char c = line[i];
    char next = i + 1 < len ? line[i + 1] : '\0';

    if (escaped) {
      escaped = false;
      continue;
    }

    if (quote != QUOTE_SINGLE && quote != QUOTE_ANSI_C && c == '\\') {
      escaped = true;
      continue;
    }

    if (quote == QUOTE_NONE && c == '#') {
      return i;
    }

    if (quote == QUOTE_NONE && c == '$' && next == '\'') {
      quote = QUOTE_ANSI_C;
      i++;
    } else if (quote == QUOTE_NONE && c == '\'') {
      quote = QUOTE_SINGLE;
    } else if (quote == QUOTE_SINGLE && c == '\'') {
      quote = QUOTE_NONE;
    } else if (quote == QUOTE_ANSI_C && c == '\'') {
      quote = QUOTE_NONE;
    } else if (quote == QUOTE_NONE && c == '"') {
      quote = QUOTE_DOUBLE;
    } else if (quote == QUOTE_DOUBLE && c == '"') {
      quote = QUOTE_NONE;
    } else if (quote == QUOTE_NONE && c == '`') {
      quote = QUOTE_BACKTICK;
    } else if (quote == QUOTE_BACKTICK && c == '`') {
      quote = QUOTE_NONE;
    }
  }

  return len;
}

static size_t read_heredoc_delimiter(const char *line, size_t len, size_t *index, char *delimiter) {
  size_t i = *index;
  size_t out = 0;
  Quote quote = QUOTE_NONE;

  while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;

  while (i < len && out + 1 < MAX_DELIMITER_LEN) {
    char c = line[i];
    if (quote == QUOTE_NONE && (c == ' ' || c == '\t' || c == ';' || c == '&' || c == '|')) break;

    if (quote == QUOTE_NONE && c == '\'') {
      quote = QUOTE_SINGLE;
      i++;
      continue;
    }
    if (quote == QUOTE_SINGLE && c == '\'') {
      quote = QUOTE_NONE;
      i++;
      continue;
    }
    if (quote == QUOTE_NONE && c == '"') {
      quote = QUOTE_DOUBLE;
      i++;
      continue;
    }
    if (quote == QUOTE_DOUBLE && c == '"') {
      quote = QUOTE_NONE;
      i++;
      continue;
    }
    if (quote != QUOTE_SINGLE && c == '\\' && i + 1 < len) {
      i++;
      c = line[i];
    }

    delimiter[out++] = c;
    i++;
  }

  delimiter[out] = '\0';
  *index = i;
  return out;
}

static void update_bash_state(Scanner *scanner, const char *line, size_t len) {
  scanner->trailing_operator = false;
  scanner->trailing_backslash = false;

  if (scanner->heredoc_count > 0 && scanner->quote == QUOTE_NONE &&
      scanner->paren_depth == 0 && scanner->brace_depth == 0 &&
      scanner->double_bracket_depth == 0) {
    if (line_matches_heredoc(scanner, line, len)) {
      pop_heredoc(scanner);
    }
    return;
  }

  size_t code_len = shell_comment_start(line, len);
  const char *last_word = "";
  size_t last_word_len = 0;
  bool escaped = false;

  for (size_t i = 0; i < code_len; i++) {
    char c = line[i];
    char next = i + 1 < code_len ? line[i + 1] : '\0';

    if (escaped) {
      escaped = false;
      continue;
    }

    if (scanner->quote != QUOTE_SINGLE && scanner->quote != QUOTE_ANSI_C && c == '\\') {
      escaped = true;
      continue;
    }

    if (scanner->quote == QUOTE_NONE && c == '$' && next == '\'') {
      scanner->quote = QUOTE_ANSI_C;
      i++;
      continue;
    }
    if (scanner->quote == QUOTE_NONE && c == '\'') {
      scanner->quote = QUOTE_SINGLE;
      continue;
    }
    if (scanner->quote == QUOTE_SINGLE && c == '\'') {
      scanner->quote = QUOTE_NONE;
      continue;
    }
    if (scanner->quote == QUOTE_ANSI_C && c == '\'') {
      scanner->quote = QUOTE_NONE;
      continue;
    }
    if (scanner->quote == QUOTE_NONE && c == '"') {
      scanner->quote = QUOTE_DOUBLE;
      continue;
    }
    if (scanner->quote == QUOTE_DOUBLE && c == '$' && next == '(') {
      if (i + 2 < code_len && line[i + 2] == '(') {
        scanner->paren_depth += 2;
        i += 2;
      } else {
        scanner->paren_depth++;
        i++;
      }
      continue;
    }
    if (scanner->quote == QUOTE_DOUBLE && c == ')' && scanner->paren_depth > 0) {
      scanner->paren_depth--;
      continue;
    }
    if (scanner->quote == QUOTE_DOUBLE && c == '"') {
      scanner->quote = QUOTE_NONE;
      continue;
    }
    if (scanner->quote == QUOTE_NONE && c == '`') {
      scanner->quote = QUOTE_BACKTICK;
      continue;
    }
    if (scanner->quote == QUOTE_BACKTICK && c == '`') {
      scanner->quote = QUOTE_NONE;
      continue;
    }

    if (scanner->quote != QUOTE_NONE) continue;

    if (c == '<' && next == '<') {
      bool strip_tabs = false;
      i += 2;
      if (i < code_len && line[i] == '-') {
        strip_tabs = true;
        i++;
      }
      char delimiter[MAX_DELIMITER_LEN];
      size_t delimiter_len = read_heredoc_delimiter(line, code_len, &i, delimiter);
      if (delimiter_len > 0) {
        push_heredoc(scanner, delimiter, delimiter_len, strip_tabs);
      }
      if (i > 0) i--;
      continue;
    }

    if (c == '[' && next == '[') {
      scanner->double_bracket_depth++;
      i++;
      continue;
    }
    if (c == ']' && next == ']' && scanner->double_bracket_depth > 0) {
      scanner->double_bracket_depth--;
      i++;
      continue;
    }
    if (c == '(') {
      scanner->paren_depth++;
      continue;
    }
    if (c == ')' && scanner->paren_depth > 0) {
      scanner->paren_depth--;
      continue;
    }
    if (c == '{') {
      scanner->brace_depth++;
      continue;
    }
    if (c == '}' && scanner->brace_depth > 0) {
      scanner->brace_depth--;
      continue;
    }

    if (is_ident_start(c)) {
      size_t start = i;
      i++;
      while (i < code_len && is_ident_char(line[i])) i++;
      size_t word_len = i - start;

      if (opens_compound(line + start, word_len)) {
        scanner->compound_depth++;
      } else if (closes_compound(line + start, word_len) && scanner->compound_depth > 0) {
        scanner->compound_depth--;
      }

      last_word = line + start;
      last_word_len = word_len;
      i--;
      continue;
    }
  }

  scanner->trailing_backslash = scanner->quote != QUOTE_SINGLE &&
                                scanner->quote != QUOTE_ANSI_C &&
                                has_unescaped_trailing_backslash(line, code_len);
  scanner->trailing_operator = scanner->quote == QUOTE_NONE &&
                               trailing_control_operator(line, code_len, last_word, last_word_len);
}

static void advance(TSLexer *lexer) {
  lexer->advance(lexer, false);
}

static bool scan_newline(TSLexer *lexer) {
  if (lexer->lookahead == '\r') {
    advance(lexer);
    if (lexer->lookahead == '\n') advance(lexer);
    return true;
  }
  if (lexer->lookahead == '\n') {
    advance(lexer);
    return true;
  }
  return false;
}

static bool scan_line_text(TSLexer *lexer, char **line, size_t *len) {
  size_t capacity = 64;
  *len = 0;
  *line = malloc(capacity);
  if (!*line) return false;

  while (lexer->lookahead && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
    if (*len + 1 >= capacity) {
      capacity *= 2;
      char *new_line = realloc(*line, capacity);
      if (!new_line) {
        free(*line);
        *line = NULL;
        return false;
      }
      *line = new_line;
    }
    (*line)[(*len)++] = (char)lexer->lookahead;
    advance(lexer);
  }
  (*line)[*len] = '\0';
  return *len > 0;
}

void *tree_sitter_console_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  scanner->at_line_start = true;
  return scanner;
}

void tree_sitter_console_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_console_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  memcpy(buffer, scanner, sizeof(Scanner));
  return sizeof(Scanner);
}

void tree_sitter_console_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  if (length == sizeof(Scanner)) {
    memcpy(scanner, buffer, sizeof(Scanner));
  } else {
    memset(scanner, 0, sizeof(Scanner));
    scanner->at_line_start = true;
  }
}

bool tree_sitter_console_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  if (valid_symbols[CONTINUATION_NEWLINE] && scanner->at_line_start &&
      needs_more_bash(scanner) && scan_newline(lexer)) {
    lexer->mark_end(lexer);
    lexer->result_symbol = CONTINUATION_NEWLINE;
    scanner->at_line_start = true;
    return true;
  }

  if (valid_symbols[NEWLINE] && scan_newline(lexer)) {
    lexer->mark_end(lexer);
    lexer->result_symbol = NEWLINE;
    scanner->at_line_start = true;
    return true;
  }

  if (scanner->at_line_start && valid_symbols[CONTINUATION_TEXT] && needs_more_bash(scanner)) {
    char *line = NULL;
    size_t len = 0;
    if (scan_line_text(lexer, &line, &len)) {
      update_bash_state(scanner, line, len);
      free(line);
      lexer->mark_end(lexer);
      lexer->result_symbol = CONTINUATION_TEXT;
      scanner->at_line_start = false;
      return true;
    }
    free(line);
  }

  if (scanner->at_line_start && valid_symbols[COMMAND_PROMPT] && lexer->lookahead == '$') {
    advance(lexer);
    if (lexer->lookahead == ' ') advance(lexer);
    lexer->mark_end(lexer);
    lexer->result_symbol = COMMAND_PROMPT;
    scanner->at_line_start = false;
    reset_command_state(scanner);
    return true;
  }

  if (valid_symbols[COMMAND_TEXT]) {
    char *line = NULL;
    size_t len = 0;
    if (scan_line_text(lexer, &line, &len)) {
      update_bash_state(scanner, line, len);
      free(line);
      lexer->mark_end(lexer);
      lexer->result_symbol = COMMAND_TEXT;
      scanner->at_line_start = false;
      return true;
    }
    free(line);
  }

  if (scanner->at_line_start && valid_symbols[OUTPUT_TEXT]) {
    char *line = NULL;
    size_t len = 0;
    if (scan_line_text(lexer, &line, &len)) {
      free(line);
      lexer->mark_end(lexer);
      lexer->result_symbol = OUTPUT_TEXT;
      scanner->at_line_start = false;
      return true;
    }
    free(line);
  }

  return false;
}
