((command
  (command_text)? @injection.content
  (continuation
    (continuation_text) @injection.content)*)
 (#set! injection.language "bash"))
