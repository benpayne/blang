// U6: CLI flag parsing across forms (--k=v, --flag, -s, positionals). golden.
import cli;
fn main() -> int {
    Array<string> argv = ["prog", "--output=out.txt", "--verbose", "-f", "src.b", "extra", "--level=3"];
    println("output = {}", flag_value(argv, "output", "?"));
    println("level = {}", flag_value(argv, "level", "0"));
    println("verbose = {}", has_flag(argv, "verbose"));
    println("f = {}", has_flag(argv, "f"));
    println("f bool = {}", bool_flag(argv, "f"));
    println("nope = {}", has_flag(argv, "nope"));
    Array<string> pos = positionals(argv);
    print("positionals:");
    for p in pos { print(" {}", p); }
    println("");
    return 0;
}
