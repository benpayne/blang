// Build-system fixture: a binary project that imports a stdlib module.
// `bcc build` resolves `import timer;` to stdlib/timer.b and combines it into
// the program (runtime libs are linked automatically). The handler ticks three
// times, then stops the loop so timer.run() returns and main exits 0.
//
//   cd test_build/timerapp && bcc build && ./timerapp

import timer;

fn main() -> int {
	sync int ticks = 0;

	on timer.every(5) {
		ticks = ticks + 1;
		println("tick {}", ticks);
		if ticks >= 3 {
			timer.stop();
		}
	}

	timer.run();
	return 0;
}
