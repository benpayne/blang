// recv() returns Option<T>; a match that handles only some() and ignores the
// none (closed/empty) case is non-exhaustive and must be rejected. This is the
// safety payoff of surfacing the closed signal as Option.

fn main() -> int {
	chan<int> ch;
	ch.send(1);
	match ch.recv() {
		some(v) {
			return v;
		}
	}
	return 0;
}
