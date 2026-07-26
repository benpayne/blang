// E2E test: Filesystem convenience functions
// Tests: write_all, read_all, append, info, exists, is_dir, file_size, mkdir, list_dir, remove

import fs;

fn main() -> int {
	string path = "/tmp/blang_test_fs_conv.txt";
	string dir_path = "/tmp/blang_test_fs_dir";

	// --- write_all + read_all roundtrip ---
	fs.write_all(path, "hello fs");
	string content = fs.read_all(path);
	assert content == "hello fs", "write_all/read_all roundtrip failed";

	// --- append ---
	fs.append(path, " appended");
	string content2 = fs.read_all(path);
	assert content2 == "hello fs appended", "append failed";

	// --- info struct ---
	fs.FileInfo fi = fs.info(path);
	assert fi.exists() == true, "info: exists should be true";
	assert fi.is_file() == true, "info: is_file should be true";
	assert fi.is_dir() == false, "info: is_dir should be false";
	assert fi.get_size() == 17, "info: size should be 17";

	// --- convenience wrappers ---
	assert fs.exists(path) == true, "exists should be true";
	assert fs.is_dir(path) == false, "is_dir should be false for file";
	long sz = fs.file_size(path);
	assert sz == 17, "file_size should be 17";

	// --- mkdir + is_dir + list_dir ---
	fs.mkdir(dir_path);
	assert fs.is_dir(dir_path) == true, "mkdir: should be directory";

	// Create a file inside the directory
	fs.write_all(dir_path + "/test_entry.txt", "entry");
	Array<string> entries = fs.list_dir(dir_path);
	assert entries.length == 1, "list_dir should have 1 entry";

	// --- Cleanup ---
	fs.remove(dir_path + "/test_entry.txt");
	fs.remove(dir_path);
	fs.remove(path);

	assert fs.exists(path) == false, "cleanup: file should be gone";
	assert fs.exists(dir_path) == false, "cleanup: dir should be gone";

	println("codegen_fs_convenience: all assertions passed");
	return 0;
}
