package frontend

import (
	"strings"
	"testing"
)

func TestParseModuleSuccess(t *testing.T) {
	source := "module test::mod;\nlet answer: int = 42;"

	result, err := ParseModule(source)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if !result.Success {
		t.Fatalf("expected success, got diagnostics: %+v", result.Diagnostics)
	}
}

func TestParseModuleEmptySource(t *testing.T) {
	if _, err := ParseModule(""); err == nil {
		t.Fatal("expected error for empty source")
	}
}

func TestEmitIRSuccess(t *testing.T) {
	source := "module demo;\nfunc main() -> int { return 0; }"

	result, err := EmitIR(source)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if !result.Success {
		t.Fatalf("expected success, got diagnostics: %+v", result.Diagnostics)
	}

	if result.IR == "" {
		t.Fatal("expected non-empty IR output")
	}

	if want := "func main"; !strings.Contains(result.IR, want) {
		t.Fatalf("expected IR to contain %q, got %q", want, result.IR)
	}
}

func TestEmitIREmptySource(t *testing.T) {
	if _, err := EmitIR(""); err == nil {
		t.Fatal("expected error for empty source")
	}
}

func TestCheckModuleSuccess(t *testing.T) {
	source := "module demo;\nlet value: int = 42;"

	result, err := CheckModule(source)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !result.Success || len(result.Diagnostics) != 0 {
		t.Fatalf("expected success, got %+v", result)
	}
}

func TestCheckModuleDuplicates(t *testing.T) {
	source := "module demo;\nlet value: int = 1;\nlet value: int = 2;"

	result, err := CheckModule(source)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if result.Success {
		t.Fatal("expected semantic failure due to duplicate binding")
	}
	if len(result.Diagnostics) == 0 {
		t.Fatal("expected diagnostics for duplicates")
	}
}

func TestEvaluateBindingsSuccess(t *testing.T) {
	source := "module demo;\nlet a: int = 5;\nlet b: int = a + 3;"

	result, err := EvaluateBindings(source)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !result.Success {
		t.Fatalf("expected success, got diagnostics: %+v", result.Diagnostics)
	}
	if len(result.Bindings) != 2 {
		t.Fatalf("expected two bindings, got %d", len(result.Bindings))
	}
	byName := make(map[string]float64)
	for _, binding := range result.Bindings {
		if !binding.Evaluated {
			t.Fatalf("binding %s not evaluated: %+v", binding.Name, binding)
		}
		byName[binding.Name] = binding.Value
	}
	if v := byName["a"]; v != 5 {
		t.Fatalf("expected a=5, got %g", v)
	}
	if v := byName["b"]; v != 8 {
		t.Fatalf("expected b=8, got %g", v)
	}
}

func TestEvaluateBindingsEmptySource(t *testing.T) {
	if _, err := EvaluateBindings(""); err == nil {
		t.Fatal("expected error for empty source")
	}
}
