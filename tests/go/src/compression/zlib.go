package compression

/*
#cgo LDFLAGS: -lz
#include <zlib.h>
*/
import "C"

func Version() string { return C.GoString(C.zlibVersion()) }
