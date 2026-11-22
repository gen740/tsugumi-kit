package main

import (
	"os"
	"strings"

	"tsugumi"
)

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
	path := "normalized.txt"
	source, err := tsugumi.RequireInput[tsugumi.InputText](input, "text")
	if err != nil {
		return tsugumi.Output{}, err
	}
	text := strings.Join(strings.Fields(source.Value), " ") + "\n"
	if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
		return tsugumi.Output{}, tsugumi.Fail("output_invalid", err.Error())
	}
	if err := tsugumi.SendValue("Wrote " + path); err != nil {
		return tsugumi.Output{}, err
	}
	return tsugumi.Output{File: &path}, nil
}
