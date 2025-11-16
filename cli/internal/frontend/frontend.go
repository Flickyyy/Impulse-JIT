package frontend

/*
#cgo CXXFLAGS: -I${SRCDIR}/../../../frontend/include
#cgo LDFLAGS: -L${SRCDIR}/../../../build/frontend -L${SRCDIR}/../../../build/ir -limpulse-frontend -limpulse-ir -lstdc++ -lm
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

// IRResult captures the textual IR in addition to parser diagnostics.
type IRResult struct {
	Success     bool
	IR          string
	Diagnostics []Diagnostic
}

// CheckResult represents semantic analysis outcome.
type CheckResult struct {
	Success     bool
	Diagnostics []Diagnostic
}

func convertDiagnostics(ptr *C.struct_ImpulseParseDiagnostic, count C.size_t) []Diagnostic {
	if ptr == nil || count == 0 {
		return nil
	}
	slice := (*[1 << 30]C.struct_ImpulseParseDiagnostic)(unsafe.Pointer(ptr))[:count:count]
	result := make([]Diagnostic, count)
	for i := 0; i < int(count); i++ {
		diag := slice[i]
		result[i] = Diagnostic{
			Message: C.GoString(diag.message),
			Line:    uint64(diag.line),
			Column:  uint64(diag.column),
		}
	}
	return result
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
		Success:     bool(cResult.success),
		Diagnostics: convertDiagnostics(cResult.diagnostics, cResult.diagnostic_count),
	}

	return result, nil
}

// EmitIR parses the module and, if successful, returns the textual IR.
func EmitIR(source string) (IRResult, error) {
	if source == "" {
		return IRResult{}, errors.New("empty source")
	}

	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	options := C.struct_ImpulseParseOptions{
		path:   nil,
		source: cSource,
	}

	cResult := C.impulse_emit_ir(&options)
	defer C.impulse_free_ir_result(&cResult)

	result := IRResult{
		Success:     bool(cResult.success),
		Diagnostics: convertDiagnostics(cResult.diagnostics, cResult.diagnostic_count),
	}

	if cResult.ir_text != nil {
		result.IR = C.GoString(cResult.ir_text)
	}

	return result, nil
}

// CheckModule runs parsing + semantic analysis and returns diagnostics.
func CheckModule(source string) (CheckResult, error) {
	if source == "" {
		return CheckResult{}, errors.New("empty source")
	}

	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	options := C.struct_ImpulseParseOptions{
		path:   nil,
		source: cSource,
	}

	cResult := C.impulse_check_module(&options)
	defer C.impulse_free_semantic_result(&cResult)

	return CheckResult{
		Success:     bool(cResult.success),
		Diagnostics: convertDiagnostics(cResult.diagnostics, cResult.diagnostic_count),
	}, nil
}
