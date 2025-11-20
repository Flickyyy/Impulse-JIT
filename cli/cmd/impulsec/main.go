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
	evaluate := flag.Bool("evaluate", false, "Evaluate constant bindings and print their values")
	evalBinding := flag.String("eval-binding", "", "Evaluate only the specified binding (implies --evaluate)")
	runModule := flag.Bool("run", false, "Run the module by treating a constant binding as the entry point")
	entryBinding := flag.String("entry-binding", "", "Binding name to use as the entry point (defaults to 'main')")
	flag.Parse()

	if *sourcePath == "" {
		fmt.Fprintln(os.Stderr, "usage: impulsec --file <path>")
		os.Exit(1)
	}

	if *entryBinding != "" {
		*runModule = true
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

	if *runModule {
		entry := *entryBinding
		if entry == "" {
			entry = "main"
		}
		runResult, err := frontend.RunModule(string(source), entry)
		if err != nil {
			fmt.Fprintf(os.Stderr, "run error: %v\n", err)
			os.Exit(1)
		}
		if !runResult.Success {
			fmt.Fprintf(os.Stderr, "run failed\n")
			if len(runResult.Diagnostics) > 0 {
				printDiagnostics(runResult.Diagnostics)
			}
			if runResult.Message != "" {
				fmt.Fprintf(os.Stderr, "%s\n", runResult.Message)
			}
			os.Exit(2)
		}
		if len(runResult.Diagnostics) > 0 {
			printDiagnostics(runResult.Diagnostics)
		}
		if runResult.HasExitCode {
			fmt.Printf("Program exited with %d\n", runResult.ExitCode)
		} else {
			fmt.Println("Program ran successfully")
		}
		if runResult.Message != "" {
			fmt.Println(runResult.Message)
		}
		return
	}

	if *evaluate || *evalBinding != "" {
		bindingName := *evalBinding
		evalResult, err := frontend.EvaluateBindings(string(source))
		if err != nil {
			fmt.Fprintf(os.Stderr, "evaluation error: %v\n", err)
			os.Exit(1)
		}
		if !evalResult.Success {
			fmt.Fprintf(os.Stderr, "evaluation failed with %d diagnostics:\n", len(evalResult.Diagnostics))
			printDiagnostics(evalResult.Diagnostics)
			os.Exit(2)
		}
		if len(evalResult.Diagnostics) > 0 {
			printDiagnostics(evalResult.Diagnostics)
		}

		if bindingName != "" {
			if !printSingleBinding(evalResult.Bindings, bindingName) {
				fmt.Fprintf(os.Stderr, "binding %q not found\n", bindingName)
				os.Exit(3)
			}
		} else {
			printAllBindings(evalResult.Bindings)
		}
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

func printAllBindings(bindings []frontend.BindingValue) {
	for _, binding := range bindings {
		printBinding(binding)
	}
}

func printSingleBinding(bindings []frontend.BindingValue, name string) bool {
	found := false
	for _, binding := range bindings {
		if binding.Name != name {
			continue
		}
		printBinding(binding)
		found = true
		break
	}
	return found
}

func printBinding(binding frontend.BindingValue) {
	if binding.Evaluated {
		fmt.Printf("%s = %g\n", binding.Name, binding.Value)
		return
	}
	message := "not constant"
	if binding.Message != "" {
		message = binding.Message
	}
	fmt.Printf("%s = <unevaluated> (%s)\n", binding.Name, message)
}
