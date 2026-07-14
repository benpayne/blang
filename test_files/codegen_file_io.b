// E2E test: File I/O operations
// Tests: open, write, read_line, read_all, seek, tell, size, close, exists, remove

import fs;

fn main() -> int {
	string path = "/tmp/blang_test_file_io.txt";

	// --- Write two lines ---
	fs.File f = fs.open(path, "w");
	f.write("hello world\n");
	f.write("second line\n");
	f.close();

	// --- Read line by line ---
	fs.File reader = fs.open(path, "r");
	string line1 = reader.read_line();
	string line2 = reader.read_line();

	// Verify content (lines include trailing newline)
	assert line1 == "hello world\n", "line1 mismatch";
	assert line2 == "second line\n", "line2 mismatch";
	reader.close();

	// --- read_all ---
	fs.File reader2 = fs.open(path, "r");
	string all = reader2.read_all();
	assert all == "hello world\nsecond line\n", "read_all mismatch";
	reader2.close();

	// --- seek / tell / size ---
	fs.File reader3 = fs.open(path, "r");
	long pos0 = reader3.tell();
	assert pos0 == 0, "initial tell should be 0";

	long sz = reader3.size();
	assert sz == 24, "file size should be 24 bytes";

	// Seek to offset 6 and read partial
	long offset = 6;
	reader3.seek(offset, 0);
	string partial = reader3.read(5);
	assert partial == "world", "partial read mismatch";

	reader3.close();

	// --- exists + remove ---
	bool e = fs.exists(path);
	assert e == true, "file should exist";

	bool removed = fs.remove(path);
	assert removed == true, "remove should succeed";

	bool e2 = fs.exists(path);
	assert e2 == false, "file should not exist after remove";

	println("codegen_file_io: all assertions passed");
	return 0;
}
