package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/Flickyyy/Impulse-JIT/cli/internal/frontend"
)

func main() {
	sourcePath := flag.String("file", "", "Impulse source file to parse")
	emitIR := flag.Bool("emit-ir", false, "Emit textual IR after parsing")
	check := flag.Bool("check", false, "Run semantic checks")
	flag.Parse()

	if *sourcePath == "" {
		fmt.Fprintln(os.Stderr, "usage: impulsec --file <path>")
		os.Exit(1)
	}

	source, err := os.ReadFile(*sourcePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to read source: %v\n", err)
		os.Exit(1)
	}

	if *emitIR {
		irResult, err := frontend.EmitIR(string(source))
		if err != nil {
			fmt.Fprintf(os.Stderr, "IR emission error: %v\n", err)
			os.Exit(1)
		}
		if !irResult.Success {
			fmt.Fprintf(os.Stderr, "IR emission failed with %d diagnostics:\n", len(irResult.Diagnostics))
			printDiagnostics(irResult.Diagnostics)
			os.Exit(2)
		}
		fmt.Println(irResult.IR)
		return
		return
	}

	if *check {
		checkResult, err := frontend.CheckModule(string(source))
		if err != nil {
			fmt.Fprintf(os.Stderr, "semantic error: %v\n", err)
			os.Exit(1)
		}
		if !checkResult.Success {
			fmt.Fprintf(os.Stderr, "semantic check failed with %d diagnostics:\n", len(checkResult.Diagnostics))
			printDiagnostics(checkResult.Diagnostics)
			os.Exit(2)
		}
		fmt.Println("Semantic checks passed")
		return
	}

	result, err := frontend.ParseModule(string(source))
	if err != nil {
		fmt.Fprintf(os.Stderr, "parser error: %v\n", err)
		os.Exit(1)
	}

	if !result.Success {
		fmt.Fprintf(os.Stderr, "parse failed with %d diagnostics:\n", len(result.Diagnostics))
		printDiagnostics(result.Diagnostics)
		os.Exit(2)
	}

	fmt.Println("Parse successful")
}

func printDiagnostics(diags []frontend.Diagnostic) {
	for _, diag := range diags {
		fmt.Fprintf(os.Stderr, "[%d:%d] %s\n", diag.Line, diag.Column, diag.Message)
	}
}
