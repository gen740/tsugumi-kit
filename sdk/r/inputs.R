# Only the inputs that arrived are present. `input$port` and
# `input[["port"]]` read an input the Card needs; `optional_input` reads one
# it can do without.
`[[.tsugumi_input` <- function(x, i, ...) {
  values <- unclass(x)
  if (is.character(i) && length(i) == 1L && !(i %in% names(values))) {
    stop(structure(class = c("tsugumi_failure", "error", "condition"), list(
      message = paste("Missing input", i), call = NULL, kind = "input_missing"
    )))
  }
  values[[i, ...]]
}
`$.tsugumi_input` <- function(x, name) {
  if (identical(name, "params")) attr(x, "params") else x[[name]]
}
optional_input <- function(input, name) {
  values <- unclass(input)
  if (name %in% names(values)) values[[name]] else NULL
}
