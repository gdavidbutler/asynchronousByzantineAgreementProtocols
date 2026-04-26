#!awk -f
#
# Local translator: emit a C fragment from a dtc .psu in which every test is
# on a '~'-prefixed boundary name.  The fragment is meant to be #include'd
# inside an existing C function whose locals match the post-'~' identifiers.
#
# Each value (right-hand side) in the bridge is a literal C token (macro,
# integer literal, etc.) and is emitted verbatim.  Unprefixed R lines are
# domain intermediates from the optimizer's trace; they are emitted as
# comments at the leaf where each fires, documenting the C in the .dtc's
# vocabulary.  Unprefixed T lines are not expected in a clean bridge.
#
# Usage:
#   awk -f psu.awk file.psu > file.c
#
# Output is a bare snippet: labels, gotos, tests, assignments.  Caller
# wraps it with declarations of the boundary locals and any pre/post
# side-effect code.

# Parse a CSV line into fields[] (RFC 4180: doubled quotes inside quotes).
function parse_csv(line, fields,    n, i, c, in_quote, field) {
  n = 0
  i = 1
  while (i <= length(line)) {
    field = ""
    in_quote = 0
    c = substr(line, i, 1)
    if (c == "\"") {
      in_quote = 1
      i++
      while (i <= length(line)) {
        c = substr(line, i, 1)
        if (c == "\"") {
          if (substr(line, i + 1, 1) == "\"") {
            field = field "\""
            i += 2
          } else {
            i++
            break
          }
        } else {
          field = field c
          i++
        }
      }
      if (substr(line, i, 1) == ",")
        i++
    } else {
      while (i <= length(line)) {
        c = substr(line, i, 1)
        if (c == ",") {
          i++
          break
        }
        field = field c
        i++
      }
    }
    fields[++n] = field
  }
  return n
}

# Two-pass: first collect jump targets so we can suppress dead labels.
/^[LJTR],/ { body[++nbody] = $0 }
/^T,/ {
  parse_csv($0, f)
  jt[f[4]] = 1
}
/^J,/ {
  parse_csv($0, f)
  jt[f[2]] = 1
}

END {
  for (i = 1; i <= nbody; i++) {
    line = body[i]
    parse_csv(line, f)
    if (line ~ /^L,/) {
      lab = f[2]
      if (lab != "0" && jt[lab])
        print "L" lab ":;"
    } else if (line ~ /^T,/) {
      var = f[2]; val = f[3]; lab = f[4]
      if (substr(var, 1, 1) == "~") {
        print "  if (" substr(var, 2) " == " val ")"
        print "    goto L" lab ";"
      } else {
        # Should not occur in a clean bridge; surface as a build-stopping comment.
        print "#error \"unbridged test on '" var "' = '" val "' -> L" lab "\""
      }
    } else if (line ~ /^J,/) {
      lab = f[2]
      if (lab == "0")
        print "  goto Ldone;"
      else
        print "  goto L" lab ";"
    } else if (line ~ /^R,/) {
      var = f[2]; val = f[3]
      if (substr(var, 1, 1) == "~") {
        print "  " substr(var, 2) " = " val ";"
      } else {
        # Domain intermediate — preserve in the spec's vocabulary at this leaf.
        gsub(/\*\//, "* /", var); gsub(/\*\//, "* /", val)
        print "  /* \"" var "\" = " val " */"
      }
    }
  }
  # Single exit label so 'J,0' resolves cleanly even when label 0 is suppressed.
  print "Ldone:;"
}
