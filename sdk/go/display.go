package tsugumi

/*
#include <stdlib.h>
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
	"strings"
	"unsafe"
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

type displayMessage struct {
	view  string
	bytes []byte
}

// ViewSession identifies an open window. It can be passed to another Card.
type ViewSession [16]byte

// Route identifies a process mailbox for a window.
type Route [16]byte

type ViewMode uint32

const (
	Panel ViewMode = iota
	Window
	Fullscreen
)

// ViewOpen opens a declared view and returns the opener's route and session.
// Window mode can specify both dimensions in logical pixels or leave both zero.
func ViewOpen(view string, mode ViewMode, width, height uint32) (Route, ViewSession, error) {
	var route Route
	var session ViewSession
	if strings.IndexByte(view, 0) >= 0 {
		return route, session, fmt.Errorf("view name contains a NUL byte")
	}
	name := C.CString(view)
	defer C.free(unsafe.Pointer(name))
	if C.tsugumi_go_view_open(name, C.uint32_t(mode), C.uint32_t(width), C.uint32_t(height),
		bytesPointer(route[:]), bytesPointer(session[:])) != 0 {
		return Route{}, ViewSession{}, fmt.Errorf("the host rejected the view open")
	}
	return route, session, nil
}

// RouteRegister creates a route for this process on an existing window.
func RouteRegister(session ViewSession) (Route, error) {
	var route Route
	if C.tsugumi_go_route_register(bytesPointer(session[:]), bytesPointer(route[:])) != 0 {
		return Route{}, fmt.Errorf("the host rejected the route registration")
	}
	return route, nil
}

// RouteUnregister removes a route owned by this process.
func RouteUnregister(route Route) error {
	if C.tsugumi_go_route_unregister(bytesPointer(route[:])) != 0 {
		return fmt.Errorf("the host rejected the route unregistration")
	}
	return nil
}

// Send sends one opaque message to a window, including between run calls.
func Send(session ViewSession, bytes []byte) error {
	return sendBytes(session, bytes, 0)
}

func sendBytes(session ViewSession, bytes []byte, framed C.int) error {
	if C.tsugumi_go_send(bytesPointer(session[:]), bytesPointer(bytes), C.size_t(len(bytes)), framed) != 0 {
		return fmt.Errorf("the host rejected the view message")
	}
	return nil
}

type ViewEventKind uint32

const (
	Message ViewEventKind = iota
	Close
)

// ViewEvent is delivered to one route. Data is empty on close.
type ViewEvent struct {
	Kind    ViewEventKind
	Route   Route
	Session ViewSession
	Data    []byte
}

// ReceiveStatus describes the result of receiving from the process mailbox.
type ReceiveStatus int

const (
	Received ReceiveStatus = iota
	Timeout
	CancelledReceive
)

// Recv waits for a message or close event on any route of this process.
// A negative timeout waits without a deadline. The returned bytes are owned by Go.
func Recv(timeoutMS int64) (ViewEvent, ReceiveStatus, error) {
	var event ViewEvent
	var kind C.uint32_t
	var data *C.uint8_t
	var size C.size_t
	status := C.tsugumi_go_recv(C.int64_t(timeoutMS), &kind,
		bytesPointer(event.Route[:]), bytesPointer(event.Session[:]), &data, &size)
	defer C.free(unsafe.Pointer(data))
	switch status {
	case 0:
		event.Kind = ViewEventKind(kind)
		event.Data = C.GoBytes(unsafe.Pointer(data), C.int(size))
		return event, Received, nil
	case 1:
		return ViewEvent{}, Timeout, nil
	case 2:
		return ViewEvent{}, CancelledReceive, nil
	default:
		return ViewEvent{}, Received, fmt.Errorf("the host rejected the view receive")
	}
}

// SendValue opens the bundled view for a typed value and sends the value to it.
func SendValue(value any) error {
	message, err := encodeDisplay(value)
	if err != nil {
		return err
	}
	_, session, err := ViewOpen(message.view, Panel, 0, 0)
	if err != nil {
		return err
	}
	return sendBytes(session, message.bytes, 1)
}

// SendValueTo sends a typed value to an already open bundled HTML view.
func SendValueTo(session ViewSession, value any) error {
	message, err := encodeDisplay(value)
	if err != nil {
		return err
	}
	return sendBytes(session, message.bytes, 1)
}

func configuredMessage(name string, config any, binaryData ...[]byte) (displayMessage, error) {
	bytes, err := json.Marshal(config)
	if err != nil {
		return displayMessage{}, err
	}
	if len(binaryData) != 0 {
		if uint64(len(bytes)) > math.MaxUint32 {
			return displayMessage{}, fmt.Errorf("display metadata is too large")
		}
		payload := binary.LittleEndian.AppendUint32(nil, uint32(len(bytes)))
		payload = append(payload, bytes...)
		for _, part := range binaryData {
			payload = append(payload, part...)
		}
		bytes = payload
	}
	return displayMessage{name, bytes}, nil
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

func encodeDisplay(value any) (displayMessage, error) {
	switch value := value.(type) {
	case string:
		return configuredMessage("text", map[string]string{"text": value})
	case Markdown:
		return configuredMessage("markdown", map[string]Markdown{"text": value})
	case Scalar:
		return configuredMessage("markdown", map[string]string{"text": scalarText(value.Label, scalarValue(value.Value))})
	case float32:
		return encodeDisplay(Scalar{Value: float64(value)})
	case float64:
		return encodeDisplay(Scalar{Value: value})
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64:
		return configuredMessage("markdown", map[string]string{"text": scalarText("", fmt.Sprint(value))})
	case Table:
		rows := make([][]string, 0, len(value.Rows))
		for _, row := range value.Rows {
			if len(row) != len(value.Columns) {
				return displayMessage{}, fmt.Errorf("display table rows must match the columns")
			}
			rows = append(rows, append([]string{}, row...))
		}
		return configuredMessage("table", map[string]any{
			"columns": append([]string{}, value.Columns...), "rows": rows,
		})
	case Image:
		return configuredMessage("image", map[string]string{"media_type": value.MediaType, "alt": value.Alt},
			value.Bytes)
	case Points:
		count := len(value.X)
		if len(value.Y) != count || len(value.Z) != count || (len(value.Colors) != 0 && len(value.Colors) != count) {
			return displayMessage{}, fmt.Errorf("display point coordinates and colors must have equal lengths")
		}
		return configuredMessage("points", map[string]int{"count": count},
			pointBytes(value.X), pointBytes(value.Y), pointBytes(value.Z), pointBytes(value.Colors))
	case LineChart:
		return lineChartMessage(value)
	case Plotly:
		return displayMessage{"plotly", value}, nil
	case VegaLite:
		return displayMessage{"vega-lite", value}, nil
	default:
		return displayMessage{}, fmt.Errorf("unsupported display value %T", value)
	}
}

func pointBytes[T float32 | uint32](values []T) []byte {
	encoded, _ := binary.Append(make([]byte, 0, 4*len(values)), binary.LittleEndian, values)
	return encoded
}

func lineChartMessage(chart LineChart) (displayMessage, error) {
	isNonFinite := func(value float64) bool { return math.IsNaN(value) || math.IsInf(value, 0) }
	series := make([]map[string]any, 0, len(chart.Series))
	var x, y []byte
	for _, row := range chart.Series {
		if len(row.X) != len(row.Y) || slices.ContainsFunc(row.X, isNonFinite) || slices.ContainsFunc(row.Y, isNonFinite) {
			return displayMessage{}, fmt.Errorf("display line coordinates must be finite and have equal lengths")
		}
		series = append(series, map[string]any{
			"count": len(row.X), "id": row.ID, "label": row.Label, "offset": len(x) / 8,
		})
		x, _ = binary.Append(x, binary.LittleEndian, row.X)
		y, _ = binary.Append(y, binary.LittleEndian, row.Y)
	}
	return configuredMessage("line-chart", map[string]any{"series": series}, x, y)
}
