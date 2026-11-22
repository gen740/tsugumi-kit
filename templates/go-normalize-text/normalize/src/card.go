package main

import (
	"os"
	"strings"

	"tsugumi"
)

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
	const path = "normalized.txt"
	text := strings.Join(strings.Fields(input.Text().Value), " ") + "\n"
	if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
		return tsugumi.Output{}, tsugumi.Fail("output_invalid", err.Error())
	}
	if err := tsugumi.SendValue("Wrote " + path); err != nil {
		return tsugumi.Output{}, err
	}
	return tsugumi.Output{File: &tsugumi.OutputFile{Value: path}}, nil
}
