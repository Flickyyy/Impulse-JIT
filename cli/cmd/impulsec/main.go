package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/Flickyyy/Impulse-JIT/cli/internal/frontend"
)

func main() {
	sourcePath := flag.String("file", "", "Impulse source file to parse")
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

	result, err := frontend.ParseModule(string(source))
	if err != nil {
		fmt.Fprintf(os.Stderr, "parser error: %v\n", err)
		os.Exit(1)
	}

	if !result.Success {
		fmt.Fprintf(os.Stderr, "parse failed with %d diagnostics:\n", len(result.Diagnostics))
		for _, diag := range result.Diagnostics {
			fmt.Fprintf(os.Stderr, "[%d:%d] %s\n", diag.Line, diag.Column, diag.Message)
		}
		os.Exit(2)
	}

	fmt.Println("Parse successful")
}
