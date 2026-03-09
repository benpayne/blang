struct Counter {
    int count;
}

impl Counter {
    fn increment(self) {
        self.count = self.count + 1;
    }

    fn get_count(self) -> int {
        return self.count;
    }
}

fn main() -> int {
    Counter c = Counter { count: 0 };
    c.increment();
    return c.get_count();
}
