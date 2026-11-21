package main

import (
	"testing"

	"tsugumi"
)

func TestDevelopmentSdk(t *testing.T) {
	if !tsugumi.Cancelled() || tsugumi.RemainingMS() != 0 {
		t.Fatal("expected inactive effects without a host")
	}
}
