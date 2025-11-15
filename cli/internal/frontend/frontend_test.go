package frontend

import "testing"

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
