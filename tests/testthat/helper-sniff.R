# Helpers and fixtures for test-sniff.R, kept out of the test file so the
# suite runs shuffled (#29).

sniff_text <- function(x, ...) sniff_csv(csv_file(x), ...)

# every shape that exercises a different branch of the pass
FIXTURES <- c(
  header        = "id,name,score\n1,Ada,9.5\n2,Grace,8\n",
  headerless    = "1,2,3\n4,5,6\n",
  all_character = "x,y,z\nx,y,z\n",
  quoted_names  = "\"id\",\"2024\"\n1,5\n",
  empty_names   = ",\n1,2\n",
  header_only   = "a,b\n",
  one_record    = "1,2\n",
  blanks        = "\n\na,b\n1,2\n\n",
  logicals      = "a,b\nTRUE,FALSE\nfalse,true\n",
  widening      = "a\n1\n2.5\n",
  na_cells      = "a,b\n1,NA\n2,\n",
  empty_file    = ""
)
