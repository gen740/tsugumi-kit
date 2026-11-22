main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- "note.txt"
  writeLines(input$text$value, file)
  list(file = file)
}
