struct Counter {
    int count;
}

impl Counter {
    fn increment(self) {
        self.count = self.count + 1;
    }

    fn add(self, int n) {
        self.count = self.count + n;
    }

    fn get_count(self) -> int {
        return self.count;
    }
}

fn main() -> int {
    Counter c = Counter { count: 0 };

    // Test basic field assignment via method
    c.increment();
    if c.get_count() != 1 { return 1; }

    // Test with arguments
    c.add(5);
    if c.get_count() != 6 { return 2; }

    // Test multiple increments
    c.increment();
    c.increment();
    if c.get_count() != 8 { return 3; }

    // Test field access after mutation
    if c.count != 8 { return 4; }

    // Test direct field assignment from outside
    c.count = 100;
    if c.count != 100 { return 5; }

    // Test compound assignment
    c.count += 10;
    if c.count != 110 { return 6; }

    println("Field assignment test passed!");
    return 0;
}
