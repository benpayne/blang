// stdlib/fs.b — BLang filesystem standard library
//
// Usage: import fs;
//   fs.open(path, mode) -> File
//   fs.read_all(path) -> string
//   fs.write_all(path, data) -> int
//   fs.append(path, data) -> int
//   fs.info(path) -> FileInfo
//   fs.exists(path) -> bool
//   fs.is_dir(path) -> bool
//   fs.file_size(path) -> long
//   fs.remove(path) -> bool
//   fs.mkdir(path) -> bool
//   fs.list_dir(path) -> Array<string>
//
// File objects support: read, write, close (FileOps), plus
// read_into, read_line, read_all, flush, seek, tell, size.

// C runtime declarations (internal — users should not call these directly)
extern fn __blang_file_open(cstring path, cstring mode) -> int;
extern fn __blang_file_close(int fd);
extern fn __blang_file_write_string(int fd, string data) -> int;
extern fn __blang_file_read_into_byte_array(int fd, Array<byte> arr, long max_len) -> long;
extern fn __blang_file_seek(int fd, long offset, int whence) -> long;
extern fn __blang_file_flush(int fd) -> int;
extern fn __blang_fs_file_type(cstring path) -> int;
extern fn __blang_fs_file_size(cstring path) -> long;
extern fn __blang_fs_remove(cstring path) -> int;
extern fn __blang_fs_mkdir(cstring path) -> int;
extern fn __blang_fs_list_dir(cstring path) -> Array<string>;

// String <-> byte array helpers
extern fn __blang_string_copy_to_byte_array(string s, Array<byte> arr);
extern fn __blang_string_from_byte_array(Array<byte> arr, long start, long end) -> string;

// --- FileOps protocol (also defined in net.b for Socket/ServerSocket) ---
pub protocol FileOps {
	fn read(self, int max_len) -> string;
	fn write(self, string data) -> int;
	fn close(self);
}

// --- FileInfo struct (returned by single stat call) ---
pub struct FileInfo {
	int type;
	long size;
}

impl FileInfo {
	pub fn exists(self) -> bool { return self.type >= 0; }
	pub fn is_dir(self) -> bool { return self.type == 1; }
	pub fn is_file(self) -> bool { return self.type == 0; }
	pub fn get_size(self) -> long { return self.size; }
}

// --- File struct ---
pub struct File {
	int _fd;
}

impl FileOps for File {
	pub fn read(self, int max_len) -> string {
		long max_long = max_len;
		Buffer buf = Buffer(max_len);
		long n = __blang_file_read_into_byte_array(self._fd, buf.get_bytes(), max_long);
		if n <= 0 { return ""; }
		return buf.to_string();
	}
	pub fn write(self, string data) -> int {
		return __blang_file_write_string(self._fd, data);
	}
	pub fn close(self) { __blang_file_close(self._fd); }
}

impl File {
	fn read_into(self, Buffer buf, long max_len) -> long {
		return __blang_file_read_into_byte_array(self._fd, buf.get_bytes(), max_len);
	}

	fn read_line(self) -> string {
		Buffer buf = Buffer(256);
		int done = 0;
		long one = 1;
		while done == 0 {
			long n = self.read_into(buf, one);
			if n <= 0 { done = 1; }
			if done == 0 {
				int last_byte = buf.get(buf.get_length() - 1);
				if last_byte == 10 { done = 1; }
			}
		}
		return buf.to_string();
	}

	fn read_all(self) -> string {
		Buffer buf = Buffer(4096);
		int done = 0;
		long chunk = 4096;
		while done == 0 {
			long n = self.read_into(buf, chunk);
			if n <= 0 { done = 1; }
		}
		return buf.to_string();
	}

	fn flush(self) -> int { return __blang_file_flush(self._fd); }

	fn seek(self, long offset, int whence) -> long {
		return __blang_file_seek(self._fd, offset, whence);
	}

	fn tell(self) -> long {
		long zero = 0;
		return self.seek(zero, 1);
	}

	fn size(self) -> long {
		long zero = 0;
		long cur = self.seek(zero, 1);
		long end_pos = self.seek(zero, 2);
		self.seek(cur, 0);
		return end_pos;
	}

	// Return the raw file descriptor for use with sendfile.
	fn get_fd(self) -> int { return self._fd; }
}

// --- Free functions ---

pub fn open(string path, string mode) -> File {
	int fd = __blang_file_open(path, mode);
	return File { _fd: fd };
}

pub fn read_all(string path) -> string {
	File f = open(path, "r");
	string data = f.read_all();
	f.close();
	return data;
}

pub fn write_all(string path, string data) -> int {
	File f = open(path, "w");
	int n = f.write(data);
	f.close();
	return n;
}

pub fn append(string path, string data) -> int {
	File f = open(path, "a");
	int n = f.write(data);
	f.close();
	return n;
}

pub fn info(string path) -> FileInfo {
	int t = __blang_fs_file_type(path);
	long s = __blang_fs_file_size(path);
	return FileInfo { type: t, size: s };
}
pub fn exists(string path) -> bool {
	int t = __blang_fs_file_type(path);
	return t >= 0;
}
pub fn is_dir(string path) -> bool {
	int t = __blang_fs_file_type(path);
	return t == 1;
}
pub fn file_size(string path) -> long {
	return __blang_fs_file_size(path);
}
pub fn remove(string path) -> bool { return __blang_fs_remove(path) == 0; }
pub fn mkdir(string path) -> bool { return __blang_fs_mkdir(path) == 0; }
pub fn list_dir(string path) -> Array<string> { return __blang_fs_list_dir(path); }
