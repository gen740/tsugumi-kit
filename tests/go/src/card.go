package main

import (
	"errors"
	"os"
	"syscall"

	"example.org/go-card/compression"
	"tsugumi"
	input "tsugumi/inputs/data"
	mergeNumber "tsugumi/outputs/arrival/tsugumi/merge/t0/nested/t0"
	mergeReading "tsugumi/outputs/arrival/tsugumi/merge/t1"
	output "tsugumi/outputs/data"
	"tsugumi/outputs/number"
)

var background chan error

func displaySamples() error {
	bytes := []byte{0, 13, 255}
	samples := []any{
		"résumé\n\"",
		tsugumi.Markdown("**text**"),
		2.5,
		tsugumi.Table{Columns: []string{"n"}, Rows: [][]string{{"1"}}},
		tsugumi.Image{MediaType: "image/png", Bytes: bytes, Alt: "sample"},
		tsugumi.Points{X: []float32{1, -2}, Y: []float32{3, 4}, Z: []float32{5, 6}, Colors: []uint32{0x123456, 0xaabbccdd}},
		tsugumi.LineChart{Series: []tsugumi.Series{
			{ID: "a", Label: "first", X: []float64{2, -4}, Y: []float64{3, 5}},
			{ID: "b", Label: "second", X: []float64{6}, Y: []float64{-7}},
		}},
		tsugumi.Plotly(`{"data":[]}`),
		tsugumi.VegaLite(`{"mark":"point"}`),
	}
	for _, sample := range samples {
		if err := tsugumi.SendValue(sample); err != nil {
			return err
		}
	}
	return tsugumi.Send("custom", bytes)
}

func run(in tsugumi.Inputs) (tsugumi.Output, error) {
	var out tsugumi.Output
	switch in.Params().Mode {
	case 1:
		if compression.Version() == "" {
			return out, errors.New("zlib returned an empty version")
		}
		active := make(chan bool)
		go func() { active <- !tsugumi.Cancelled() && tsugumi.RemainingMS() == 500 }()
		if !<-active {
			return out, errors.New("active effects failed")
		}
		if err := displaySamples(); err != nil {
			return out, err
		}
		background = make(chan error, 1)
		go func(fd int, done chan<- error) {
			done <- tsugumi.Guard(func() error {
				var signal [1]byte
				n, err := syscall.Read(fd, signal[:])
				if err != nil || n != 1 || !tsugumi.Cancelled() || tsugumi.RemainingMS() != 0 {
					return errors.New("inactive effects failed")
				}
				return tsugumi.SendValue("outside")
			})
		}(int(in.Params().Fd), background)
	case 2:
		if err := <-background; err != nil {
			return out, err
		}
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
		out.Number = &tsugumi.OutputNumber{Position: &number.PositionT{X: 42, Y: -15}}
		out.Data = &tsugumi.OutputData{}
		return out, nil
	case 8:
		if !tsugumi.Cancelled() || tsugumi.RemainingMS() != -1 {
			return out, errors.New("cancelled effects failed")
		}
		return out, tsugumi.Fail("cancelled", "host cancelled")
	case 9:
		done := make(chan error, 1)
		go func() {
			done <- tsugumi.Guard(func() error { panic("background panic") })
		}()
		return out, <-done
	}

	if value, err := tsugumi.OptionalInput[string](in, "unwired"); value != nil || err != nil {
		return out, errors.New("unwired input should be absent")
	}
	if in.Params().Mode == 20 {
		_, err := tsugumi.RequireInput[string](in, "unwired")
		return out, err
	}
	data, err := tsugumi.RequireInput[tsugumi.InputData](in, "data")
	if err != nil {
		return out, err
	}
	numberInput, err := tsugumi.RequireInput[tsugumi.InputNumber](in, "number")
	if err != nil {
		return out, err
	}
	choice, ok := data.Choice.Value.(*input.TextT)
	if data.Choice.Type != input.ChoiceText || !ok {
		return out, errors.New("input union was not decoded")
	}
	position := numberInput.Position
	out.Number = &tsugumi.OutputNumber{Position: &number.PositionT{
		X: position.X + data.Rows[0].Value,
		Y: position.Y,
	}}
	out.Data = &tsugumi.OutputData{
		Rows: []*output.RowT{{Value: data.Rows[0].Value}},
		Choice: &output.ChoiceT{
			Type:  output.ChoiceCount,
			Value: &output.CountT{Value: int64(len(choice.Text))},
		},
	}
	if file := in.File(); file != nil {
		content, err := os.ReadFile(*file)
		if err != nil {
			return out, err
		}
		if err := os.WriteFile("result.txt", content, 0600); err != nil {
			return out, err
		}
		path := "result.txt"
		out.File = &path
		out.Forwarded = file
	}
	if directory := in.Directory(); directory != nil {
		out.Directory = directory
	}
	if arrival := in.Arrival(); arrival != nil {
		if arrival.Merged != nil {
			out.Arrival = &tsugumi.OutputArrival{Merged: &tsugumi.OutputArrivalMerged{
				Temperature: &mergeNumber.TemperatureT{Value: arrival.Merged.Temperature.Value + 1},
			}}
		} else {
			out.Arrival = &tsugumi.OutputArrival{Reading: &mergeReading.ReadingT{Name: arrival.Reading.Name}}
		}
	}
	if in.Params().Mode == 24 {
		out.Arrival = &tsugumi.OutputArrival{}
	}
	return out, nil
}
