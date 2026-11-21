package main

import (
	"errors"
	"os"
	"syscall"

	"tsugumi"
	input "tsugumi/inputs/data"
	output "tsugumi/outputs/data"
)

var background chan struct{}

func displaySamples() error {
	bytes := []byte{0, 13, 255}
	samples := []struct {
		value  any
		record bool
	}{
		{value: "résumé\n\""},
		{tsugumi.Markdown("**text**"), true},
		{value: 2.5},
		{value: tsugumi.Table{Columns: []string{"n"}, Rows: [][]string{{"1"}}}},
		{value: tsugumi.Image{MediaType: "image/png", Bytes: bytes, Alt: "sample"}},
		{value: tsugumi.Points{X: []float32{1}, Y: []float32{1}, Z: []float32{1}, Colors: []uint32{0x123456}}},
		{value: tsugumi.LineChart{Series: []tsugumi.Series{{ID: "id", Label: "line", X: []float64{2}, Y: []float64{2}}}}},
		{value: tsugumi.Plotly(`{"data":[]}`)},
		{value: tsugumi.VegaLite(`{"mark":"point"}`)},
		{tsugumi.Module{Module: "custom", Config: []byte(`{"key":1}`), Blobs: []tsugumi.Blob{{Name: "sample", Bytes: bytes}}}, true},
	}
	for _, sample := range samples {
		var err error
		if sample.record {
			err = tsugumi.Display(sample.value, true)
		} else {
			err = tsugumi.Display(sample.value)
		}
		if err != nil {
			return err
		}
	}
	return nil
}

func run(in tsugumi.Inputs) (tsugumi.Output, error) {
	var out tsugumi.Output
	switch in.Params().Mode {
	case 1:
		active := make(chan bool)
		go func() { active <- !tsugumi.Cancelled() && tsugumi.RemainingMS() == 500 }()
		if !<-active {
			return out, errors.New("active effects failed")
		}
		if err := displaySamples(); err != nil {
			return out, err
		}
		background = make(chan struct{})
		go func(fd int, done chan struct{}) {
			defer close(done)
			var signal [1]byte
			n, err := syscall.Read(fd, signal[:])
			if err != nil || n != 1 || !tsugumi.Cancelled() || tsugumi.RemainingMS() != 0 {
				panic("inactive effects failed")
			}
			if err := tsugumi.Display("outside"); err != nil {
				panic(err)
			}
		}(int(in.Params().Fd), background)
	case 2:
		<-background
	case 3:
		failure := tsugumi.Fail("cancelled", "explicit failure")
		failure.Cell = "fit"
		return out, failure
	case 4:
		return out, errors.New("ordinary error")
	case 5:
		panic("test panic")
	case 6:
		return out, nil
	case 7:
		out.Number = &tsugumi.OutputNumber{Value: 42}
		out.Data = &tsugumi.OutputData{}
		return out, nil
	case 8:
		if !tsugumi.Cancelled() || tsugumi.RemainingMS() != -1 {
			return out, errors.New("cancelled effects failed")
		}
		return out, tsugumi.Fail("cancelled", "host cancelled")
	}

	data := in.Data()
	choice, ok := data.Choice.Value.(*input.TextT)
	if data.Choice.Type != input.ChoiceText || !ok {
		return out, errors.New("input union was not decoded")
	}
	out.Number = &tsugumi.OutputNumber{Value: in.Number().Value + data.Rows[0].Value}
	out.Data = &tsugumi.OutputData{
		Rows: []*output.RowT{{Value: data.Rows[0].Value}},
		Choice: &output.ChoiceT{
			Type:  output.ChoiceCount,
			Value: &output.CountT{Value: int64(len(choice.Text))},
		},
	}
	if in.File() != nil {
		content, err := os.ReadFile(in.File().Value)
		if err != nil {
			return out, err
		}
		if err := os.WriteFile("result.txt", content, 0600); err != nil {
			return out, err
		}
		out.File = &tsugumi.OutputFile{Value: "result.txt"}
		out.Forwarded = &tsugumi.OutputForwarded{Value: in.File().Value}
	}
	return out, nil
}
