package frontend

// #cgo CXXFLAGS: -I${SRCDIR}/../../../frontend/include -I${SRCDIR}/../../../ir/include
// #cgo LDFLAGS: -L${SRCDIR}/../../../build/frontend -L${SRCDIR}/../../../build/ir -limpulse-frontend -limpulse-ir -lstdc++ -lm
// #include <stdlib.h>
// #include "../../../frontend/include/impulse/frontend/frontend.h"
import "C"

import (
	"errors"
	"unsafe"
)

// BindingValue represents evaluation outcome for a single binding.
type BindingValue struct {
	Name      string
	Evaluated bool
	Value     float64
	Message   string
}

// EvalResult contains interpreter output plus diagnostics.
type EvalResult struct {
	Success     bool
	Diagnostics []Diagnostic
	Bindings    []BindingValue
}

func convertBindingValues(ptr *C.struct_ImpulseBindingValue, count C.size_t) []BindingValue {
	if ptr == nil || count == 0 {
		return nil
	}
	slice := (*[1 << 30]C.struct_ImpulseBindingValue)(unsafe.Pointer(ptr))[:count:count]
	result := make([]BindingValue, count)
	for i := 0; i < int(count); i++ {
		value := slice[i]
		result[i] = BindingValue{
			Name:      C.GoString(value.name),
			Evaluated: bool(value.evaluated),
			Value:     float64(value.value),
		}
		if value.message != nil {
			result[i].Message = C.GoString(value.message)
		}
	}
	return result
}

func EvaluateBindings(source string) (EvalResult, error) {
	if source == "" {
		return EvalResult{}, errors.New("empty source")
	}

	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	options := C.struct_ImpulseParseOptions{
		path:   nil,
		source: cSource,
	}

	cResult := C.impulse_evaluate_bindings(&options)
	defer C.impulse_free_eval_result(&cResult)

	return EvalResult{
		Success:     bool(cResult.success),
		Diagnostics: convertDiagnostics(cResult.diagnostics, cResult.diagnostic_count),
		Bindings:    convertBindingValues(cResult.bindings, cResult.binding_count),
	}, nil
}
