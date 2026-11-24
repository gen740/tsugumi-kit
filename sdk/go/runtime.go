// Package tsugumi contains the generated Card types and process-wide effects.
package tsugumi

/*
#include "host.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"path"
	"runtime"
	"strings"
	"unsafe"

	wire "tsugumi/Tsugumi/Card"
	"tsugumi/Tsugumi/Data"
	"tsugumi/flatbuffers"
	artifact "tsugumi/tsugumi"
)

// Failure carries a Card failure kind and an optional definition-relative location.
type Failure struct {
	Kind, Message string
	Source        string
	Line          uint32
	Cell          string
}

func (failure *Failure) Error() string { return failure.Message }

// Fail creates an error annotated with the position of its caller.
func Fail(kind, message string) *Failure {
	failure := &Failure{Kind: kind, Message: message}
	if _, source, line, ok := runtime.Caller(1); ok {
		if relative, found := strings.CutPrefix(source, cardModule+"/"); found {
			failure.Source = path.Join(sourcePrefix, relative)
			failure.Line = uint32(line)
		}
	}
	return failure
}

// Cancelled is process-wide and returns true between calls.
func Cancelled() bool { return C.tsugumi_go_cancelled() != 0 }

// RemainingMS returns 0 between calls or -1 for an active call without a deadline.
func RemainingMS() int64 { return int64(C.tsugumi_go_remaining_ms()) }

func bytesPointer(bytes []byte) *C.uint8_t {
	return (*C.uint8_t)(unsafe.Pointer(unsafe.SliceData(bytes)))
}

func stringPointer(value string) *C.char {
	// C++ copies the bytes synchronously. Give empty strings a non-null pointer.
	if value == "" {
		value = "\x00"
	}
	return (*C.char)(unsafe.Pointer(unsafe.StringData(value)))
}

func byKind(err error, fallback string) (failure *Failure) {
	failure = &Failure{Kind: fallback, Message: "Go error reporting panicked"}
	defer func() { _ = recover() }()
	var explicit *Failure
	if errors.As(err, &explicit) && explicit != nil {
		return explicit
	}
	failure.Message = err.Error()
	return failure
}

func writeFailure(raw unsafe.Pointer, failure *Failure) int {
	kind, ok := Data.EnumValuesFailureKind[failure.Kind]
	if !ok || kind == Data.FailureKindunspecified {
		kind = Data.FailureKindworker_failed
	}
	source, line := path.Clean(failure.Source), failure.Line
	if source == "." || source == ".." || strings.HasPrefix(source, "../") || path.IsAbs(source) {
		source, line = "", 0
	}
	return int(C.tsugumi_go_failure(raw, C.uint16_t(kind),
		stringPointer(failure.Message), C.size_t(len(failure.Message)),
		stringPointer(source), C.size_t(len(source)), C.uint32_t(line),
		stringPointer(failure.Cell), C.size_t(len(failure.Cell))))
}

func catchPanic(err *error) {
	if recovered := recover(); recovered != nil {
		*err = fmt.Errorf("Go panic: %v", recovered)
	}
}

// Guard runs work in the calling goroutine and converts a panic to an error.
// Use it inside the goroutine and handle the returned error there or send it
// to another goroutine. A panic in a goroutine started by work needs its own guard.
func Guard(work func() error) (err error) {
	defer catchPanic(&err)
	return work()
}

func invoke(data []byte, result unsafe.Pointer, run func(Inputs) (Output, error)) (stage string, err error) {
	stage = "input_invalid"
	defer catchPanic(&err)

	input, err := readInput(wire.GetRootAsCardInput(data, 0))
	if err != nil {
		return stage, err
	}
	stage = "worker_failed"
	output, err := run(input)
	if err != nil {
		return stage, err
	}
	stage = "output_invalid"
	return stage, output.send(result)
}

// InvokeRun is called only by the generated adapter. No Go pointer is retained by C.
func InvokeRun(data unsafe.Pointer, size uintptr, result unsafe.Pointer, run func(Inputs) (Output, error)) int {
	// The shared ABI verifier has already bounded size below the C.int limit.
	stage, err := invoke(C.GoBytes(data, C.int(size)), result, run)
	if err != nil {
		return writeFailure(result, byKind(err, stage))
	}
	return 0
}

func verifyInput(index int, data []byte) bool {
	return C.tsugumi_go_verify_input(C.size_t(index), bytesPointer(data), C.size_t(len(data))) != 0
}

func writeOutput(result unsafe.Pointer, index int, data []byte) bool {
	return C.tsugumi_go_output(result, C.size_t(index), bytesPointer(data), C.size_t(len(data))) != 0
}

// OptionalInput returns the typed value for name, or nil if it did not arrive.
func OptionalInput[T any](input Inputs, name string) (*T, error) {
	value := input.value(name)
	if value == nil {
		return nil, nil
	}
	if typed, ok := value.(*T); ok {
		return typed, nil
	}
	return nil, &Failure{Kind: "invalid_request", Message: fmt.Sprintf("Wrong Go type for input %s", name)}
}

// RequireInput returns a value or an input_missing failure that names the port.
func RequireInput[T any](input Inputs, name string) (T, error) {
	value, err := OptionalInput[T](input, name)
	if value != nil {
		return *value, nil
	}
	var zero T
	if err != nil {
		return zero, err
	}
	return zero, &Failure{Kind: "input_missing", Message: fmt.Sprintf("Missing input %s", name)}
}

func pack(value interface {
	Pack(*flatbuffers.Builder) flatbuffers.UOffsetT
}) []byte {
	builder := flatbuffers.NewBuilder(256)
	builder.Finish(value.Pack(builder))
	return builder.FinishedBytes()
}

// Artifact is the native object for a tsugumi.Artifact field.
type Artifact = artifact.ArtifactT

// ArtifactOption changes how Dir and Upload create an Artifact.
type ArtifactOption func(*artifactOptions)

type artifactOptions struct{ keep bool }

// Keep sets whether the Artifact is kept after its run (the default). With
// Keep(false) it exists only while its run runs: later Cards of the same run
// can open it, and it is removed with its record when the run ends.
func Keep(keep bool) ArtifactOption {
	return func(options *artifactOptions) { options.keep = keep }
}

func keepOption(options []ArtifactOption) bool {
	selected := artifactOptions{keep: true}
	for _, option := range options {
		option(&selected)
	}
	return selected.keep
}

// Dir creates an Artifact and returns its writable directory.
func Dir(options ...ArtifactOption) (*Artifact, string, error) {
	return artifact.Dir(keepOption(options))
}

// Upload copies a file or directory into a new Artifact.
func Upload(source string, options ...ArtifactOption) (*Artifact, error) {
	return artifact.Upload(source, keepOption(options))
}

func unknownInput() error { return errors.New("unknown input port") }
