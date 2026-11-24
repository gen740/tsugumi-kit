package main

import (
	"os"
	"path/filepath"
	"strings"

	"tsugumi"
)

func run(_ tsugumi.Inputs) (tsugumi.Output, error) {
	name := "normalized.txt"
	params := tsugumi.Parameters()
	text := strings.Join(strings.Fields(params.Value), " ") + "\n"
	artifact, dir, err := tsugumi.Dir(tsugumi.Keep(params.KeepOutput))
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
