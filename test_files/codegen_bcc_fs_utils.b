// Stdlib-via-bcc (functional-hardening U4 / REQ-004): fs File methods exercised
// THROUGH THE REAL bcc DRIVER. test_codegen.sh never invokes bcc (it calls
// qcc --combine directly), so "compiles+runs through bcc" is the new signal.
// Centered on `read_into` (the one File method with no existing codegen test),
// plus seek/tell/size for numeric, deterministic goldens. Printed AND asserted.

import fs;

fn main() -> int {
	string path = "/tmp/blang_u4_fs_utils.txt";
	fs.write_all(path, "hello world\nsecond line\n");

	// read_into: read a bounded chunk into a Buffer (returns bytes read).
	// max_len is a `long` — pass a long variable (the stdlib's own idiom;
	// see read_all/read_line in fs.b).
	fs.File f = fs.open(path, "r");
	Buffer buf = Buffer(64);
	long want = 11;
	long n = f.read_into(buf, want);      // "hello world"
	string chunk = buf.to_string();
	println("read_into n={}", n);
	println("chunk=[{}]", chunk);
	assert n == 11, "read_into returns bytes read";
	assert chunk == "hello world", "read_into content";

	// tell after the read: cursor advanced by 11.
	long pos = f.tell();
	println("tell={}", pos);
	assert pos == 11, "tell after read_into";

	// size: total file length (does not disturb the cursor).
	long sz = f.size();
	println("size={}", sz);
	assert sz == 24, "file size";
	long pos2 = f.tell();
	assert pos2 == 11, "size preserves cursor";

	// seek back to start, read the whole file.
	long zero = 0;
	f.seek(zero, 0);
	string all = f.read_all();
	println("all_len={}", all.length);
	assert all.length == 24, "read_all length after seek(0)";
	assert all == "hello world\nsecond line\n", "read_all content";
	f.close();

	// read_line through bcc: first line includes its trailing newline.
	fs.File r = fs.open(path, "r");
	string line1 = r.read_line();
	println("line1_len={}", line1.length);
	assert line1 == "hello world\n", "read_line first line";
	r.close();

	fs.remove(path);
	println("PASS");
	return 0;
}
