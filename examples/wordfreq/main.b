// Example #4 — wordfreq: word-frequency counting with generic collections.
//
// Counts word occurrences in a text, then prints them alphabetically. This is
// the validation program for the generic-ARC unit: every value flowing through
// Map / Set / sort here is a refcounted string, which used to crash or leak.
//
// It exercises:
//   - Map<string, int>      counting (get_or + set overwrite)
//   - Set<string>           stop-word filtering (has / add)
//   - sort<T> + comparator  alphabetical order over STRINGS, via a lambda
//   - generic call inference (sort(names, ...) — no explicit <string>)
//   - string scanning       tokenize with index_of / substring

import collections;

fn main() -> int {
	string text = "the quick brown fox jumps over the lazy dog "
		+ "the dog barks and the fox runs over the hill";

	// Stop words to exclude from the count.
	Set<string> stop = Set<string>();
	stop.add("the");
	stop.add("and");
	stop.add("over");

	// Count every non-stop word.
	Map<string, int> counts = Map<string, int>();
	Array<string> words = tokenize(text);
	for w in words {
		if !stop.has(w) {
			counts.set(w, counts.get_or(w, 0) + 1);
		}
	}

	// Copy the keys (sorting in place would reorder the Map's internals) and
	// sort them alphabetically — a lambda comparator over strings, with the
	// element type inferred from the array.
	Array<string> names = [];
	Array<string> keys = counts.keys();
	for k in keys {
		names.push(k);
	}
	sort(names, fn(string a, string b) -> bool { return a < b; });

	for name in names {
		println("{}: {}", name, counts.get(name));
	}
	println("{} distinct words ({} stop words ignored)", counts.length(), stop.length());
	return 0;
}

// Split on single spaces (the only separator this example needs).
fn tokenize(string text) -> Array<string> {
	Array<string> words = [];
	string rest = text;
	while rest.length > 0 {
		int sp = rest.index_of(" ");
		if sp < 0 {
			words.push(rest);
			rest = "";
		} else {
			if sp > 0 {
				words.push(rest.substring(0, sp));
			}
			rest = rest.substring(sp + 1, rest.length);
		}
	}
	return words;
}

// --- Tests (run with `bcc test`) ------------------------------------------

test "tokenize splits on spaces" {
	Array<string> w = tokenize("alpha beta gamma");
	assert w.length == 3;
	assert w[0] == "alpha";
	assert w[2] == "gamma";
}

test "tokenize collapses repeated spaces" {
	Array<string> w = tokenize("a  b");
	assert w.length == 2;
	assert w[1] == "b";
}

test "map counts occurrences" {
	Map<string, int> counts = Map<string, int>();
	Array<string> w = tokenize("a b a c a b");
	for x in w {
		counts.set(x, counts.get_or(x, 0) + 1);
	}
	assert counts.get("a") == 3;
	assert counts.get("b") == 2;
	assert counts.get("c") == 1;
	assert counts.length() == 3;
}

test "set membership filters" {
	Set<string> s = Set<string>();
	s.add("x");
	s.add("y");
	assert s.has("x");
	assert !s.has("z");
	assert s.length() == 2;
}

test "sort orders strings with a lambda comparator" {
	Array<string> names = ["delta", "alpha", "charlie", "bravo"];
	sort(names, fn(string a, string b) -> bool { return a < b; });
	assert names[0] == "alpha";
	assert names[1] == "bravo";
	assert names[2] == "charlie";
	assert names[3] == "delta";
}

test "sort handles duplicates and stays stable in content" {
	Array<string> xs = ["b", "a", "b", "a"];
	sort(xs, fn(string a, string b) -> bool { return a < b; });
	assert xs[0] == "a";
	assert xs[1] == "a";
	assert xs[2] == "b";
	assert xs[3] == "b";
}
