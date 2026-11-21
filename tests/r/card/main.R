# The host leaves this file in place between calls.
cat("loaded\n", file = "loaded", append = TRUE)
calls <- 0L
session_file <- tempfile()
writeLines("session", session_file)

fail <- function() stop("R card failure")

main <- function(input) {
  calls <<- calls + 1L
  stopifnot(
    startsWith(tempdir(), paste0(Sys.getenv("HOME"), "/")),
    normalizePath(Sys.getenv("TMPDIR")) != normalizePath(Sys.getenv("HOME")),
    file.exists(session_file),
    identical(readLines("loaded"), "loaded")
  )
  if (input$params$mode == 3L) fail()
  if (input$params$mode == 4L) return(list())
  if (!cancelled()) {
    stopifnot(remaining_ms() == 500)
    display("R Card")
    display(scalar(2.5, "counts"), record = TRUE)
    display(data.frame(name = "value"))
  }

  stopifnot(input$number$unit == "Volts")
  row <- input$table$rows[[1L]]
  stopifnot(
    row$signed_value == "-9223372036854775808",
    row$unsigned_value == "18446744073709551615",
    identical(row$special[1:3], list("NaN", "Inf", "-Inf")),
    identical(row$bytes, "AA3/"),
    identical(row$selection, list(type = "Alias", value = list(name = "selected")))
  )
  row$bytes <- jsonlite::base64_dec(row$bytes)

  file <- paste0("result-", calls, ".txt")
  writeLines(c(readLines(input$file$value), as.character(calls)), file)
  list(number = list(value = input$number$value + calls, unit = input$number$unit),
       table = list(rows = list(row)),
       file = list(value = file),
       forwarded = input$file)
}
