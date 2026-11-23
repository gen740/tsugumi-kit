package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"example.org/go-card/compression"

	"tsugumi"
	"tsugumi/inputs/batch"
	"tsugumi/outputs"
)

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
	var output tsugumi.Output
	var granted *tsugumi.Artifact
	number := int64(20)
	if member := input.Arrival().Number; member != nil {
		number = member.Value
		granted = member.File
	} else {
		granted = input.Arrival().Text.File
	}
	opened, err := granted.Open()
	if err != nil {
		return output, err
	}
	direct, err := input.Direct().Open()
	if err != nil {
		return output, err
	}
	params := tsugumi.Parameters()
	items := input.Batch().Items
	if direct != opened || len(items) != 1 || items[0].Schema != strings.Repeat("4", 64) ||
		batch.GetRootAsElement(items[0].Bytes, 0).Value() != 4 || !params.Flag ||
		params.Ratio != 1 || params.Labels[0] != "日本語" ||
		compression.Version() == "" || tsugumi.Cancelled() || tsugumi.RemainingMS() != 500 {
		return output, fmt.Errorf("unexpected contract fixture values")
	}
	if number < 0 {
		return output, nil
	}

	nested, dir, err := tsugumi.Dir()
	if err != nil {
		return output, err
	}
	file := filepath.Join(dir, "value.txt")
	if err := os.WriteFile(file, []byte("test"), 0600); err != nil {
		return output, err
	}
	upload, err := tsugumi.Upload(file)
	if err != nil {
		return output, err
	}
	choice, _, err := tsugumi.Dir()
	if err != nil {
		return output, err
	}
	data, status, err := tsugumi.Recv("request", -1)
	if err != nil || status != tsugumi.Message || !bytes.Equal(data, []byte{42}) {
		return output, fmt.Errorf("unexpected receive message: %v", err)
	}
	if _, status, err := tsugumi.Recv("request", 0); err != nil || status != tsugumi.Timeout {
		return output, fmt.Errorf("unexpected receive timeout: %v", err)
	}
	if _, status, err := tsugumi.Recv("request", 1); err != nil || status != tsugumi.CancelledReceive {
		return output, fmt.Errorf("unexpected receive cancellation: %v", err)
	}
	for _, size := range []int{1, (1 << 24) + 5} {
		if err := tsugumi.Send("listing", bytes.Repeat([]byte{42}, size)); err != nil {
			return output, err
		}
	}
	go func() {
		reply, status, err := tsugumi.Recv("background", -1)
		if err == nil && status == tsugumi.Message {
			_ = tsugumi.Send("background", reply)
		}
	}()
	output.Listing = &outputs.ListingT{
		Value:  params.Offset + number,
		Nested: &outputs.ChildT{File: nested},
		Files:  []*tsugumi.Artifact{upload, granted},
		Choice: &outputs.ChoiceT{Type: outputs.ChoiceFile, Value: choice},
	}
	return output, nil
}
