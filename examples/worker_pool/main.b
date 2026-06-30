// Concurrent worker pool — counts primes below a limit by fanning the work out
// across green threads connected by channels.
//
// Topology:
//   producer (spawn)  -> jobs channel    -> N workers (spawn) -> results channel -> main
//   * the producer splits [0, limit) into fixed-size chunks and sends each
//     chunk's start index into the `jobs` channel, then closes it.
//   * each worker drains `jobs`, counts primes in its chunk, and sends the
//     count into `results`; when `jobs` is closed and drained, recv() yields
//     `none` and the worker exits.
//   * main is the collector: it sums one result per chunk.
//
// Exercises: spawn (one OS thread each), chan<T> send/recv/close, Option<T>
// from recv with match, a sync counter written from many threads, and wait_all.
//
// Build & run:
//   cd examples/worker_pool && bcc build && ./worker_pool

// Trial-division primality test.
fn is_prime(int n) -> bool {
	if n < 2 {
		return false;
	}
	int i = 2;
	while i * i <= n {
		if n % i == 0 {
			return false;
		}
		i = i + 1;
	}
	return true;
}

// Count primes in the half-open range [lo, hi).
fn count_primes_in_range(int lo, int hi) -> int {
	int count = 0;
	int n = lo;
	while n < hi {
		if is_prime(n) {
			count = count + 1;
		}
		n = n + 1;
	}
	return count;
}

fn main() -> int {
	int limit = 100000;
	int chunk = 2500;
	int num_workers = 4;
	int num_chunks = (limit + chunk - 1) / chunk;

	chan<int> jobs;
	chan<int> results;
	sync int chunks_done = 0;

	// Producer: enqueue each chunk's start index, then close the jobs channel.
	spawn {
		int start = 0;
		while start < limit {
			jobs.send(start);
			start = start + chunk;
		}
		jobs.close();
	}

	// Workers: drain jobs, count primes in each chunk, emit the count.
	int w = 0;
	while w < num_workers {
		spawn {
			for {
				match jobs.recv() {
					some(start) {
						int lo = start;
						if lo < 2 {
							lo = 2;
						}
						int hi = start + chunk;
						if hi > limit {
							hi = limit;
						}
						int c = count_primes_in_range(lo, hi);
						results.send(c);
						chunks_done += 1;
					}
					none {
						break;
					}
				}
			}
		}
		w = w + 1;
	}

	// Collector: one result per chunk.
	int primes = 0;
	int got = 0;
	while got < num_chunks {
		match results.recv() {
			some(c) {
				primes = primes + c;
			}
			none {
			}
		}
		got = got + 1;
	}

	// All work is in; join the producer and workers cleanly.
	wait_all;

	println("Found {} primes below {} using {} workers over {} chunks",
		primes, limit, num_workers, num_chunks);

	assert chunks_done == num_chunks, "every chunk should be processed exactly once";
	assert primes == 9592, "there are 9592 primes below 100000";

	return 0;
}
