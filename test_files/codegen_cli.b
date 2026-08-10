// U5: CLI flag parsing — stateless functional API over an argv-style array.
// Deterministic (args built in-program, not read from the environment) -> golden.
import cli;

fn main() -> int {
    // Simulate argv: program name, then flags + positionals.
    Array<string> argv = ["prog", "--name=alice", "--verbose", "-x", "input.txt", "--count=3"];

    println("name = {}", cli.flag_value(argv, "name", "?"));
    println("has verbose = {}", cli.has_flag(argv, "verbose"));
    println("verbose bool = {}", cli.bool_flag(argv, "verbose"));
    println("x bool = {}", cli.bool_flag(argv, "x"));
    println("count = {}", cli.flag_value(argv, "count", "0"));
    println("missing = {}", cli.flag_value(argv, "missing", "default"));
    println("has missing = {}", cli.has_flag(argv, "missing"));

    Array<string> pos = cli.positionals(argv);
    print("positionals:");
    for p in pos { print(" {}", p); }
    println("");
    return 0;
}
