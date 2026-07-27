// String-subject match: dispatch by content, not arm order. Previously ANY
// non-integer subject silently branched to the default arm — the design
// spec's own `match command { "start" {...} ... }` example always took the
// wildcard. Covers statement form, expression form, first/middle/last arm
// selection, no-match -> wildcard, and a variable subject reused after.
fn label(string cmd) -> string {
	return match cmd {
		"start" { "starting" }
		"stop" { "stopping" }
		"status" { "ok" }
		_ { "unknown" }
	};
}

fn code(string cmd) -> int {
	// statement form
	match cmd {
		"start" { return 1; }
		"stop" { return 2; }
		_ { return 0; }
	}
	return -1;
}

fn main() -> int {
	println("{}", label("start"));
	println("{}", label("stop"));
	println("{}", label("status"));
	println("{}", label("restart"));
	println("{}", label(""));

	println("{} {} {}", code("start"), code("stop"), code("bogus"));

	// variable subject, still usable after the match
	string cmd = "stop";
	int c = match cmd {
		"start" { 10 }
		"stop" { 20 }
		_ { 30 }
	};
	println("{} {}", c, cmd.length);
	return 0;
}
