// KI-22 (modules-v2-exports U5b, rev finding A): the iterable array temp of a
// for-in over a generic-method-call must be released exactly once on EVERY loop
// exit path — normal fall-through, `break`, and an early `return` from inside
// the loop. The first fix released it only on fall-through (manual afterBB
// release), which use-after-freed on break and leaked on early return. The
// temp is now registered in the enclosing block's array scope, so scope/return
// cleanup releases it once per path. This whole program must be --leak-check
// clean AND crash-free.

import collections;

// Early-return exit: return from inside a for-in over m.keys().
fn find_first_over(Map<string, int> m, int threshold) -> string {
	for k in m.keys() {
		if m.get(k) > threshold {
			return k;          // early return WHILE iterating the temp
		}
	}
	return "none";
}

// break exit: stop iterating a fresh Set.items() temp after the first item.
fn count_until_break(Set<string> s) -> int {
	int n = 0;
	for it in s.items() {
		n = n + 1;
		if n == 1 {
			break;             // break WHILE iterating the temp
		}
	}
	return n;
}

fn main() -> int {
	Map<string, int> m = Map<string, int>();
	m.set("a", 1);
	m.set("b", 5);
	m.set("c", 9);

	// Early-return path (returns "b", the first value > 3).
	string hit = find_first_over(m, 3);
	println("hit={}", hit);

	// Run it in a loop so any per-exit leak accumulates and any use-after-free
	// is likely to surface under MALLOC_PERTURB_/ASan.
	int rounds = 0;
	for i in 0..50 {
		string h = find_first_over(m, 0);   // returns "a" on the first key
		if h == "a" {
			rounds = rounds + 1;
		}
	}
	println("rounds={}", rounds);

	Set<string> s = Set<string>();
	s.add("x");
	s.add("y");
	s.add("z");
	println("until_break={}", count_until_break(s));

	// break inside main's own for-in over a method-call temp.
	int seen = 0;
	for k in m.keys() {
		seen = seen + 1;
		if seen == 2 {
			break;
		}
	}
	println("seen={}", seen);

	println("PASS");
	return 0;
}
