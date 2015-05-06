package lz4

// #cgo CFLAGS: -O3
// #cgo LDFLAGS: -llz4
// #include "src/lz4g.h"
// #include "src/lz4frame.h"
// #include "src/lz4.h"
import "C"

import (
	"fmt"
	"unsafe"
)

// LZ4G is what we want: see lz4g.c
//
// compressionLevel = 9 for high compression. 0 for low compression.
//
// int LZ4G_compressFramedFileStream(FILE* finput, FILE* foutput, int compressionLevel, char** errstring, int* nerrbytes)
//
// int LZ4G_decompressFramedFileStream(FILE* finput, FILE* foutput, char** errstring, int* nerrbytes)

// Uncompress with a known output size. len(out) should be equal to
// the length of the uncompressed out.
func FrameUncompress(in, out []byte) error {
	nerrbytes := 1024
	errbs := string(make([]byte, nerrbytes))

	cs := C.CString(errbs)
	defer C.free(unsafe.Pointer(cs))

	compressionLevel := C.int(9)
	fmt.Printf("%v %v\n", &cs, compressionLevel)
	//	if int(C.LZ4G_compressFramedFileStream(FILE* finput, FILE* foutput, compressionLevel, &cs, nerrbytes)
	//	if int(C.LZ4_decompress_safe(p(in), p(out), clen(in), clen(out))) != 0 {
	//		return errors.New("Malformed compression stream: '%s'", string(err))
	//	}

	return nil
}

// CompressBound calculates the size of the output buffer needed by
// Compress. This is based on the following macro:
//
// #define LZ4_COMPRESSBOUND(isize)
//      ((unsigned int)(isize) > (unsigned int)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)
func FrameCompressBound(in []byte) int {
	return len(in) + ((len(in) / 255) + 16)
}

// FrameCompress compresses in and puts the content in out. len(out)
// should have enough space for the compressed data (use FrameCompressBound
// to calculate). Returns the number of bytes in the out slice.
func FrameCompress(in, out []byte) (outSize int, err error) {
	//var pref C.LZ4F_preferences_t
	//outSize = int(C.LZ4F_compressFrameBound(srcSize, &pref))
	//	outSize = int(C.LZ4_compress_limitedOutput(p(in), p(out), clen(in), clen(out)))
	//outSize = int(C.LZ4F_compressFrame(p(in), p(out), clen(in), clen(out)))
	if outSize == 0 {
		err = fmt.Errorf("insufficient space for compression")
	}
	return
}
