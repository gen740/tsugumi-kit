# The host leaves this file in place between calls.
cat("loaded\n", file = "loaded", append = TRUE)
calls <- 0L
session_file <- tempfile()
writeLines("session", session_file)

fail <- function() stop("R card failure")

effects <- dyn.load("@native_effects@")[["name"]]

display_samples <- function(bytes) {
  invalid <- tryCatch(send(points(1, numeric(), 1)), error = conditionMessage)
  stopifnot(identical(invalid, "could not encode the display value"))
  send("r\u00e9sum\u00e9\n\"")
  send(markdown("**text**"))
  send(scalar(2.5))
  send(data.frame(n = "1"))
  send(image(bytes, alt = "sample"))
  send(points(1, 1, 1, colors = 0x123456))
  send(line_chart(list(list(id = "id", label = "line", x = 2, y = 2))))
  send(plotly(list(data = list())))
  send(vega_lite(list(mark = "point")))
  send("custom", bytes)
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
  stopifnot(is.null(optional_input(input, "unwired")))
  if (input$params$mode == 7L) input$unwired
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
            input[["params"]]$value == 17, input$params_$value == 19)
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
  writeLines(c(readLines(input$file), as.character(calls)), file)
  directory <- optional_input(input, "directory")
  if (!is.null(directory)) {
    stopifnot(identical(readLines(file.path(directory, "child")), "directory"))
  }
  arrival <- optional_input(input, "arrival")
  if (identical(arrival$type, "Temperature")) {
    arrival$value$value <- arrival$value$value + 1
  }
  list(number = list(value = input$number$value + calls, unit = input$number$unit),
       table = list(rows = list(row)),
       file = file,
       forwarded = input$file,
       directory = directory,
       arrival = arrival)
}
