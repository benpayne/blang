// stdlib/buffer.b — BLang Buffer type (pure BLang, backed by Array<byte>)
//
// Usage: import buffer;
//   Buffer buf = Buffer(64);          // create with initial capacity hint
//   Buffer buf = Buffer.from_string("hello");  // create from string
//
// Buffer is a mutable, growable byte buffer backed by Array<byte>.
// All memory is managed via ARC on the underlying array.

// C helpers for string <-> byte conversion
extern fn __blang_string_copy_to_byte_array(string s, Array<byte> arr);
extern fn __blang_string_from_byte_array(Array<byte> arr, long start, long end) -> string;

pub struct Buffer {
	Array<byte> _bytes;
}

impl Buffer {
	init(int capacity) {
		self._bytes = [];
	}

	static fn from_string(string s) -> Buffer {
		Buffer buf = Buffer(0);
		__blang_string_copy_to_byte_array(s, buf._bytes);
		return buf;
	}

	fn get_bytes(self) -> Array<byte> {
		return self._bytes;
	}

	// --- Properties ---

	fn get_length(self) -> int {
		return self._bytes.length;
	}

	fn is_empty(self) -> bool {
		return self._bytes.length == 0;
	}

	// --- Read/Write ---

	fn get(self, int index) -> int {
		byte b = self._bytes[index];
		int v = b;
		return v;
	}

	fn set(self, int index, int value) {
		byte b = value;
		self._bytes[index] = b;
	}

	// --- Append ---

	fn append_byte(self, int b) {
		byte val = b;
		self._bytes.push(val);
	}

	fn append_bytes(self, Buffer src, int len) {
		int count = len;
		int src_len = src.get_length();
		if count > src_len { count = src_len; }
		int i = 0;
		for i in 0..count {
			self.append_byte(src.get(i));
		}
	}

	fn append_string(self, string s) {
		__blang_string_copy_to_byte_array(s, self._bytes);
	}

	// --- Search ---

	fn index_of(self, Buffer pattern, int offset) -> int {
		int buf_len = self._bytes.length;
		int pat_len = pattern.get_length();
		if pat_len == 0 { return offset; }
		int end = buf_len - pat_len + 1;
		int i = offset;
		for i in offset..end {
			int found = 1;
			int j = 0;
			for j in 0..pat_len {
				int a = self.get(i + j);
				int b = pattern.get(j);
				if a != b {
					found = 0;
					j = pat_len;
				}
			}
			if found == 1 { return i; }
		}
		return -1;
	}

	// --- Slice ---

	fn slice(self, int start, int end) -> Buffer {
		Buffer result = Buffer(end - start);
		int i = start;
		for i in start..end {
			result.append_byte(self.get(i));
		}
		return result;
	}

	// --- Conversion ---

	fn to_string(self) -> string {
		long zero = 0;
		long len = self._bytes.length;
		return __blang_string_from_byte_array(self._bytes, zero, len);
	}

	fn to_string_range(self, int start, int end) -> string {
		long s = start;
		long e = end;
		return __blang_string_from_byte_array(self._bytes, s, e);
	}

	// --- Management ---

	fn clear(self) {
		self._bytes.clear();
	}

	fn compact(self, int bytes) {
		int old_len = self._bytes.length;
		int new_len = old_len - bytes;
		if new_len <= 0 {
			self._bytes.clear();
			return;
		}
		Array<byte> new_bytes = [];
		int i = bytes;
		for i in bytes..old_len {
			byte b = self._bytes[i];
			new_bytes.push(b);
		}
		self._bytes = new_bytes;
	}
}
