pub struct Map<K, V> {
    Array<K> keys;
    Array<V> values;
}

impl Map {
    fn length(self) -> int {
        return self.keys.length;
    }

    fn is_empty(self) -> bool {
        return self.keys.length == 0;
    }

    fn has(self, K key) -> bool {
        for i in 0..self.keys.length {
            if self.keys[i] == key {
                return true;
            }
        }
        return false;
    }

    fn set(self, K key, V value) {
        for i in 0..self.keys.length {
            if self.keys[i] == key {
                self.values[i] = value;
                return;
            }
        }
        self.keys.push(key);
        self.values.push(value);
    }

    fn get(self, K key) -> V {
        for i in 0..self.keys.length {
            if self.keys[i] == key {
                return self.values[i];
            }
        }
    }

    fn remove(self, K key) -> bool {
        for i in 0..self.keys.length {
            if self.keys[i] == key {
                for j in i..self.keys.length - 1 {
                    self.keys[j] = self.keys[j + 1];
                    self.values[j] = self.values[j + 1];
                }
                self.keys.pop();
                self.values.pop();
                return true;
            }
        }
        return false;
    }
}
