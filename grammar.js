module.exports = grammar({
  name: 'console',

  extras: () => [],

  externals: ($) => [
    $._command_prompt,
    $.command_text,
    $.continuation_text,
    $.output_text,
    $._newline,
    $._continuation_newline,
  ],

  rules: {
    source_file: ($) => repeat(choice($.command, $.output, $.blank_line)),

    command: ($) =>
      seq(
        field('prompt', alias($._command_prompt, $.prompt)),
        optional(field('body', $.command_text)),
        $._newline,
        repeat($.continuation),
      ),

    continuation: ($) =>
      choice(
        seq(field('body', $.continuation_text), $._newline),
        alias($._continuation_newline, $.blank_continuation),
      ),

    output: ($) => seq($.output_text, $._newline),

    blank_line: ($) => $._newline,
  },
});
