// U6: CLI flag edge cases (only positionals; missing flags; =false). golden.
import cli;
fn main() -> int {
    Array<string> only_pos = ["prog", "a", "b", "c"];
    Array<string> p = cli.positionals(only_pos);
    println("only-pos count = {}", p.length);
    println("has any flag = {}", cli.has_flag(only_pos, "x"));
    println("get_or default = {}", cli.flag_value(only_pos, "x", "DEF"));

    Array<string> with_false = ["prog", "--debug=false", "--trace"];
    println("debug bool (=false) = {}", cli.bool_flag(with_false, "debug"));
    println("trace bool = {}", cli.bool_flag(with_false, "trace"));
    return 0;
}
