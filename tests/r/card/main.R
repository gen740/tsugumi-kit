# The host leaves this file in place between calls.
cat("loaded\n", file = "loaded", append = TRUE)
calls <- 0L
session_file <- tempfile()
writeLines("session", session_file)

fail <- function() stop("R card failure")

effects <- dyn.load("@native_effects@")[["name"]]

display_samples <- function(bytes) {
  display("r\u00e9sum\u00e9\n\"")
  display(markdown("**text**"), record = TRUE)
  display(scalar(2.5))
  display(data.frame(n = "1"))
  display(image(bytes, alt = "sample"))
  display(points(1, 1, 1, colors = 0x123456))
  display(line_chart(list(list(id = "id", label = "line", x = 2, y = 2))))
  display(plotly(list(data = list())))
  display(vega_lite(list(mark = "point")))
  display(view_module("custom", list(config = charToRaw('{"key":1}'), sample = bytes)), record = TRUE)
}

main <- function(input) {
  calls <<- calls + 1L
  stopifnot(
    startsWith(tempdir(), paste0(Sys.getenv("TMPDIR"), "/")),
    dir.exists(tempdir()),
    file.exists(session_file) == (calls == 1L),
    identical(readLines("loaded"), "loaded")
  )
  writeLines("temporary", tempfile("invocation-"))
  if (input$params$mode == 3L) fail()
  if (input$params$mode == 4L) return(NULL)

  bytes <- as.raw(0:127)
  if (input$params$mode == 1L) {
    stopifnot(!cancelled(), remaining_ms() == 500)
    display_samples(bytes)
    .Call("start_effects", input$params$fd, PACKAGE = effects)
  } else if (input$params$mode == 2L) {
    .Call("join_effects", PACKAGE = effects)
    stopifnot(cancelled())
  } else if (input$params$mode == 5L) {
    stopifnot(cancelled(), remaining_ms() == 0)
  }

  stopifnot(input$number$unit == "Volts",
            input$params__$value == 17, input$params_$value == 19)
  row <- input$table$rows[[1L]]
  stopifnot(
    row$signed_value == "-9223372036854775808",
    row$unsigned_value == "18446744073709551615",
    identical(row$special[1:3], c(NaN, Inf, -Inf)),
    identical(row$single, 7),
    identical(row$empty, numeric()),
    identical(row$wide[[2L]], "18446744073709551615"),
    identical(row$position$coordinates, c(15, -14)),
    identical(row$position$depth, 10),
    identical(jsonlite::base64_dec(row$bytes), bytes),
    identical(row$selection, list(type = "Alias", value = list(name = "selected")))
  )
  row$bytes <- if (input$params$mode == 6L) "AA=?" else bytes
  row$single <- sum(row$single, row$position$coordinates)

  file <- paste0("result-", calls, ".txt")
  writeLines(c(readLines(input$file$value), as.character(calls)), file)
  if (!is.null(input$directory)) {
    stopifnot(identical(readLines(file.path(input$directory$value, "child")), "directory"))
  }
  list(number = list(value = input$number$value + calls, unit = input$number$unit),
       table = list(rows = list(row)),
       file = list(value = file),
       forwarded = input$file,
       directory = input$directory)
}
