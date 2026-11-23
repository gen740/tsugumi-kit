package main

import (
	"os"
	"path/filepath"
	"strings"

	"tsugumi"
)

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
	name := "normalized.txt"
	source, err := tsugumi.RequireInput[tsugumi.InputText](input, "text")
	if err != nil {
		return tsugumi.Output{}, err
	}
	text := strings.Join(strings.Fields(source.Value), " ") + "\n"
	artifact, dir, err := tsugumi.Dir()
	if err != nil {
		return tsugumi.Output{}, err
	}
	if err := os.WriteFile(filepath.Join(dir, name), []byte(text), 0o644); err != nil {
		return tsugumi.Output{}, tsugumi.Fail("output_invalid", err.Error())
	}
	if err := tsugumi.SendValue("Wrote " + name); err != nil {
		return tsugumi.Output{}, err
	}
	return tsugumi.Output{File: artifact}, nil
}
