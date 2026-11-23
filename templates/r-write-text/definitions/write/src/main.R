main <- function(input) {
  if (cancelled()) stop("Stopped before writing")
  file <- artifact_dir()
  writeLines(input$text$value, file.path(file$path, "note.txt"))
  list(file = file$artifact)
}
