// Package lz4 implements compression using lz4.c. This is its test
// suite.
//
// Copyright (c) 2013 CloudFlare, Inc.

package lz4

import (
	"io/ioutil"
	"strings"
	"testing"
	"testing/quick"
)

func TestFrameCompressionRatio(t *testing.T) {
	input, err := ioutil.ReadFile("sample.txt")
	if err != nil {
		t.Fatal(err)
	}
	output := make([]byte, FrameCompressBound(input))
	outSize, err := FrameCompress(input, output)
	if err != nil {
		t.Fatal(err)
	}

	if want := 4573; want != outSize {
		t.Fatalf("FrameCompressed output length != expected: %d != %d", want, outSize)
	}
}

func TestFrameCompression(t *testing.T) {
	input := []byte(strings.Repeat("Hello world, this is quite something", 10))
	output := make([]byte, FrameCompressBound(input))
	outSize, err := FrameCompress(input, output)
	if err != nil {
		t.Fatalf("FrameCompression failed: %v", err)
	}
	if outSize == 0 {
		t.Fatal("Output buffer is empty.")
	}
	output = output[:outSize]
	decompressed := make([]byte, len(input))
	err = Uncompress(output, decompressed)
	if err != nil {
		t.Fatalf("Decompression failed: %v", err)
	}
	if string(decompressed) != string(input) {
		t.Fatalf("Decompressed output != input: %q != %q", decompressed, input)
	}
}

func TestFrameEmptyCompression(t *testing.T) {
	input := []byte("")
	output := make([]byte, FrameCompressBound(input))
	outSize, err := FrameCompress(input, output)
	if err != nil {
		t.Fatalf("FrameCompression failed: %v", err)
	}
	if outSize == 0 {
		t.Fatal("Output buffer is empty.")
	}
	output = output[:outSize]
	decompressed := make([]byte, len(input))
	err = Uncompress(output, decompressed)
	if err != nil {
		t.Fatalf("Decompression failed: %v", err)
	}
	if string(decompressed) != string(input) {
		t.Fatalf("Decompressed output != input: %q != %q", decompressed, input)
	}
}

func TestFrameNoCompression(t *testing.T) {
	input := []byte("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
	output := make([]byte, FrameCompressBound(input))
	outSize, err := FrameCompress(input, output)
	if err != nil {
		t.Fatalf("FrameCompression failed: %v", err)
	}
	if outSize == 0 {
		t.Fatal("Output buffer is empty.")
	}
	output = output[:outSize]
	decompressed := make([]byte, len(input))
	err = Uncompress(output, decompressed)
	if err != nil {
		t.Fatalf("Decompression failed: %v", err)
	}
	if string(decompressed) != string(input) {
		t.Fatalf("Decompressed output != input: %q != %q", decompressed, input)
	}
}

func TestFrameCompressionError(t *testing.T) {
	input := []byte(strings.Repeat("Hello world, this is quite something", 10))
	output := make([]byte, 1)
	_, err := FrameCompress(input, output)
	if err == nil {
		t.Fatalf("FrameCompression should have failed but didn't")
	}

	output = make([]byte, 0)
	_, err = FrameCompress(input, output)
	if err == nil {
		t.Fatalf("FrameCompression should have failed but didn't")
	}
}

func TestFrameDecompressionError(t *testing.T) {
	input := []byte(strings.Repeat("Hello world, this is quite something", 10))
	output := make([]byte, FrameCompressBound(input))
	outSize, err := FrameCompress(input, output)
	if err != nil {
		t.Fatalf("FrameCompression failed: %v", err)
	}
	if outSize == 0 {
		t.Fatal("Output buffer is empty.")
	}
	output = output[:outSize]
	decompressed := make([]byte, len(input)-1)
	err = Uncompress(output, decompressed)
	if err == nil {
		t.Fatalf("Decompression should have failed")
	}

	decompressed = make([]byte, 1)
	err = Uncompress(output, decompressed)
	if err == nil {
		t.Fatalf("Decompression should have failed")
	}

	decompressed = make([]byte, 0)
	err = Uncompress(output, decompressed)
	if err == nil {
		t.Fatalf("Decompression should have failed")
	}
}

func assert(t *testing.T, b bool) {
	if !b {
		t.Fatalf("assert failed")
	}
}

func TestFrameCompressBound(t *testing.T) {
	input := make([]byte, 0)
	assert(t, FrameCompressBound(input) == 16)

	input = make([]byte, 1)
	assert(t, FrameCompressBound(input) == 17)

	input = make([]byte, 254)
	assert(t, FrameCompressBound(input) == 270)

	input = make([]byte, 255)
	assert(t, FrameCompressBound(input) == 272)

	input = make([]byte, 510)
	assert(t, FrameCompressBound(input) == 528)
}

func TestFrameFuzz(t *testing.T) {
	f := func(input []byte) bool {
		output := make([]byte, FrameCompressBound(input))
		outSize, err := FrameCompress(input, output)
		if err != nil {
			t.Fatalf("FrameCompression failed: %v", err)
		}
		if outSize == 0 {
			t.Fatal("Output buffer is empty.")
		}
		output = output[:outSize]
		decompressed := make([]byte, len(input))
		err = Uncompress(output, decompressed)
		if err != nil {
			t.Fatalf("Decompression failed: %v", err)
		}
		if string(decompressed) != string(input) {
			t.Fatalf("Decompressed output != input: %q != %q", decompressed, input)
		}

		return true
	}

	conf := &quick.Config{MaxCount: 20000}
	if testing.Short() {
		conf.MaxCount = 1000
	}
	if err := quick.Check(f, conf); err != nil {
		t.Fatal(err)
	}
}
