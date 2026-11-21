package main

import "tsugumi"

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
	return tsugumi.Output{Number: &tsugumi.OutputNumber{Value: input.Params().Value}}, nil
}
