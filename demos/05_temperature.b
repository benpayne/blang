// Demo 5: Temperature Converter with Pipeline
// Features: pipeline operator (|>), float/double arithmetic, function chaining

fn celsius_to_fahrenheit(double c) -> double {
	return c * 1.8 + 32.0;
}

fn fahrenheit_to_celsius(double f) -> double {
	return (f - 32.0) / 1.8;
}

fn celsius_to_kelvin(double c) -> double {
	return c + 273.15;
}

fn kelvin_to_celsius(double k) -> double {
	return k - 273.15;
}

fn main() -> int {
	println("=== Temperature Converter ===");
	println();

	// Water freezing point
	double water_freeze_c = 0.0;
	double water_freeze_f = water_freeze_c |> celsius_to_fahrenheit();
	double water_freeze_k = water_freeze_c |> celsius_to_kelvin();
	println("Water freezing point:");
	println("  {} C = {} F = {} K", water_freeze_c, water_freeze_f, water_freeze_k);

	// Water boiling point
	double water_boil_c = 100.0;
	double water_boil_f = water_boil_c |> celsius_to_fahrenheit();
	double water_boil_k = water_boil_c |> celsius_to_kelvin();
	println("Water boiling point:");
	println("  {} C = {} F = {} K", water_boil_c, water_boil_f, water_boil_k);

	// Body temperature
	double body_f = 98.6;
	double body_c = body_f |> fahrenheit_to_celsius();
	double body_k = body_c |> celsius_to_kelvin();
	println("Body temperature:");
	println("  {} F = {} C = {} K", body_f, body_c, body_k);

	// Absolute zero
	double abs_zero_k = 0.0;
	double abs_zero_c = abs_zero_k |> kelvin_to_celsius();
	double abs_zero_f = abs_zero_c |> celsius_to_fahrenheit();
	println("Absolute zero:");
	println("  {} K = {} C = {} F", abs_zero_k, abs_zero_c, abs_zero_f);

	println();
	println("Pipeline chaining — Celsius through all scales:");
	double c = 0.0;
	for i in 0..11 {
		double f = c |> celsius_to_fahrenheit();
		double k = c |> celsius_to_kelvin();
		println("  {} C = {} F = {} K", c, f, k);
		c = c + 10.0;
	}

	// Verify roundtrip: C -> F -> C should be identity
	double test_c = 37.0;
	double roundtrip = test_c |> celsius_to_fahrenheit() |> fahrenheit_to_celsius();
	double diff = roundtrip - test_c;
	// diff should be ~0.0 (floating point rounding)
	// Use absolute value check: diff*diff < epsilon avoids negative literal issue
	double threshold = 0.001;
	assert diff < threshold, "roundtrip should preserve value";
	assert diff * diff < threshold, "roundtrip should preserve value";

	println();
	println("Roundtrip: {} C -> F -> C = {} C (diff = {})", test_c, roundtrip, diff);

	println();
	println("Temperature converter demo passed!");
	return 0;
}
