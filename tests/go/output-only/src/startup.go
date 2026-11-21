package main

import "os"

func init() {
	file, err := os.OpenFile("go-init.marker", os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0600)
	if err != nil {
		panic(err)
	}
	if _, err := file.WriteString("loaded\n"); err != nil {
		panic(err)
	}
	if err := file.Close(); err != nil {
		panic(err)
	}
}
