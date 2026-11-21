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

func invoke(data []byte, result unsafe.Pointer, run func(Inputs) (Output, error)) (stage string, err error) {
	stage = "invalid_request"
	defer func() {
		if recovered := recover(); recovered != nil {
			err = fmt.Errorf("Go panic: %v", recovered)
		}
	}()

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
