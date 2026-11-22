package tsugumi

/*
#include "host.h"
*/
import "C"

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"slices"
	"strconv"

	wire "tsugumi/Tsugumi/Card"
	"tsugumi/flatbuffers"
)

type Markdown string

type Scalar struct {
	Value float64
	Label string
}

type Table struct {
	Columns []string
	Rows    [][]string
}

type Image struct {
	MediaType string
	Bytes     []byte
	Alt       string
}

type Points struct {
	X, Y, Z []float32
	Colors  []uint32
}

type Series struct {
	ID, Label string
	X, Y      []float64
}

type LineChart struct {
	Series []Series
}

type Plotly json.RawMessage
type VegaLite json.RawMessage

type Blob struct {
	Name  string
	Bytes []byte
}

type Module struct {
	Module string
	Config json.RawMessage
	Blobs  []Blob
}

// Display sends a value to a view module, including between run calls.
// Omit record or pass false for a live-only display.
func Display(value any, record ...bool) error {
	if len(record) > 1 {
		return fmt.Errorf("display accepts a single record flag")
	}
	module, err := displayModule(value)
	if err != nil {
		return err
	}

	builder := flatbuffers.NewBuilder(256)
	blobs := make([]*wire.CardDisplayBlobT, 0, len(module.Blobs))
	for _, blob := range module.Blobs {
		blobs = append(blobs, &wire.CardDisplayBlobT{Name: blob.Name, Bytes: blob.Bytes})
	}
	display := wire.CardDisplayT{Module: module.Module, Config: string(module.Config), Blobs: blobs}
	builder.Finish(display.Pack(builder))
	bytes := builder.FinishedBytes()
	recordFlag := C.int(0)
	if len(record) == 1 && record[0] {
		recordFlag = 1
	}
	if C.tsugumi_go_display(bytesPointer(bytes), C.size_t(len(bytes)), recordFlag) != 0 {
		return fmt.Errorf("the host rejected the display")
	}
	return nil
}

func configuredModule(name string, config any, blobs ...Blob) (Module, error) {
	bytes, err := json.Marshal(config)
	return Module{Module: "tsugumi:" + name, Config: bytes, Blobs: blobs}, err
}

func scalarValue(value float64) string {
	if math.IsNaN(value) || math.IsInf(value, 0) {
		return "\u2014"
	}
	return strconv.FormatFloat(value, 'g', 6, 64)
}

// scalarText is the markdown shown for a number: "**label**: value".
func scalarText(label, number string) string {
	if label == "" {
		return number
	}
	return "**" + label + "**: " + number
}

func displayModule(value any) (Module, error) {
	switch value := value.(type) {
	case string:
		return configuredModule("text", map[string]string{"text": value})
	case Markdown:
		return configuredModule("markdown", map[string]Markdown{"text": value})
	case Scalar:
		return configuredModule("markdown", map[string]string{"text": scalarText(value.Label, scalarValue(value.Value))})
	case float32:
		return displayModule(Scalar{Value: float64(value)})
	case float64:
		return displayModule(Scalar{Value: value})
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64:
		return configuredModule("markdown", map[string]string{"text": scalarText("", fmt.Sprint(value))})
	case Table:
		rows := make([][]string, 0, len(value.Rows))
		for _, row := range value.Rows {
			if len(row) != len(value.Columns) {
				return Module{}, fmt.Errorf("display table rows must match the columns")
			}
			rows = append(rows, append([]string{}, row...))
		}
		return configuredModule("table", map[string]any{
			"columns": append([]string{}, value.Columns...), "rows": rows,
		})
	case Image:
		return configuredModule("image", map[string]string{"media_type": value.MediaType, "alt": value.Alt},
			Blob{"image", value.Bytes})
	case Points:
		count := len(value.X)
		if len(value.Y) != count || len(value.Z) != count || (len(value.Colors) != 0 && len(value.Colors) != count) {
			return Module{}, fmt.Errorf("display point coordinates and colors must have equal lengths")
		}
		return configuredModule("points", map[string]int{"count": count},
			Blob{"x", pointBytes(value.X)}, Blob{"y", pointBytes(value.Y)},
			Blob{"z", pointBytes(value.Z)}, Blob{"colors", pointBytes(value.Colors)})
	case LineChart:
		return lineChartModule(value)
	case Plotly:
		return Module{Module: "tsugumi:plotly", Config: json.RawMessage(value)}, nil
	case VegaLite:
		return Module{Module: "tsugumi:vega-lite", Config: json.RawMessage(value)}, nil
	case Module:
		return value, nil
	default:
		return Module{}, fmt.Errorf("unsupported display value %T", value)
	}
}

func pointBytes[T float32 | uint32](values []T) []byte {
	encoded, _ := binary.Append(make([]byte, 0, 4*len(values)), binary.LittleEndian, values)
	return encoded
}

func lineChartModule(chart LineChart) (Module, error) {
	isNonFinite := func(value float64) bool { return math.IsNaN(value) || math.IsInf(value, 0) }
	series := make([]map[string]any, 0, len(chart.Series))
	var x, y []byte
	for _, row := range chart.Series {
		if len(row.X) != len(row.Y) || slices.ContainsFunc(row.X, isNonFinite) || slices.ContainsFunc(row.Y, isNonFinite) {
			return Module{}, fmt.Errorf("display line coordinates must be finite and have equal lengths")
		}
		series = append(series, map[string]any{
			"count": len(row.X), "id": row.ID, "label": row.Label, "offset": len(x) / 8,
		})
		x, _ = binary.Append(x, binary.LittleEndian, row.X)
		y, _ = binary.Append(y, binary.LittleEndian, row.Y)
	}
	return configuredModule("line-chart", map[string]any{"series": series}, Blob{"x", x}, Blob{"y", y})
}
