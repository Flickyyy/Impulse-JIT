package frontend

/*
#cgo CXXFLAGS: -I${SRCDIR}/../../../frontend/include
#cgo LDFLAGS: -L${SRCDIR}/../../../build/frontend -limpulse-frontend -lstdc++
#include <stdlib.h>
#include "../../../frontend/include/impulse/frontend/frontend.h"
*/
import "C"

import (
	"errors"
	"unsafe"
)

// Diagnostic represents a single parser diagnostic returned by the C frontend.
type Diagnostic struct {
	Message string
	Line    uint64
	Column  uint64
}

// Result contains the parsing outcome.
type Result struct {
	Success     bool
	Diagnostics []Diagnostic
}

// ParseModule runs the C++ parser on the given source string.
func ParseModule(source string) (Result, error) {
	if source == "" {
		return Result{}, errors.New("empty source")
	}

	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	options := C.struct_ImpulseParseOptions{
		path:   nil,
		source: cSource,
	}

	cResult := C.impulse_parse_module(&options)
	defer C.impulse_free_parse_result(&cResult)

	result := Result{
		Success: bool(cResult.success),
	}

	count := int(cResult.diagnostic_count)
	if count > 0 && cResult.diagnostics != nil {
		slice := (*[1 << 30]C.struct_ImpulseParseDiagnostic)(unsafe.Pointer(cResult.diagnostics))[:count:count]
		result.Diagnostics = make([]Diagnostic, count)
		for i := 0; i < count; i++ {
			diag := slice[i]
			result.Diagnostics[i] = Diagnostic{
				Message: C.GoString(diag.message),
				Line:    uint64(diag.line),
				Column:  uint64(diag.column),
			}
		}
	}

	return result, nil
}
