// U6: CLI flag parsing across forms (--k=v, --flag, -s, positionals). golden.
import cli;
fn main() -> int {
    Array<string> argv = ["prog", "--output=out.txt", "--verbose", "-f", "src.b", "extra", "--level=3"];
    println("output = {}", cli.flag_value(argv, "output", "?"));
    println("level = {}", cli.flag_value(argv, "level", "0"));
    println("verbose = {}", cli.has_flag(argv, "verbose"));
    println("f = {}", cli.has_flag(argv, "f"));
    println("f bool = {}", cli.bool_flag(argv, "f"));
    println("nope = {}", cli.has_flag(argv, "nope"));
    Array<string> pos = cli.positionals(argv);
    print("positionals:");
    for p in pos { print(" {}", p); }
    println("");
    return 0;
}
