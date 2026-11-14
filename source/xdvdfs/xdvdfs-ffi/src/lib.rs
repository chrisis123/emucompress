use std::ffi::{c_char, c_void, CStr, CString};
use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

use xdvdfs::blockdev::OffsetWrapper;
use xdvdfs::read::read_volume;
use xdvdfs::write::{
	fs::{StdFilesystem, StdIOCopier, XDVDFSFilesystem},
	img::{create_xdvdfs_image, NoOpProgressVisitor},
};

const SUCCESS: i32 = 0;
const INVALID_ARGUMENT: i32 = 1;
const OPERATION_FAILED: i32 = 2;
const PANIC: i32 = 3;
const CANCELLED: i32 = 4;

fn write_error(error_buffer: *mut u8, error_capacity: usize, message: &str) {
	if error_buffer.is_null() || error_capacity == 0 {
		return;
	}

	let bytes = message.as_bytes();
	let copy_len = bytes.len().min(error_capacity.saturating_sub(1));
	unsafe {
		std::ptr::copy_nonoverlapping(bytes.as_ptr(), error_buffer, copy_len);
		*error_buffer.add(copy_len) = 0;
	}
}

fn pack(source_path: &Path, image_path: &Path) -> Result<(), String> {
	let metadata = std::fs::metadata(source_path).map_err(|error| error.to_string())?;
	if image_path.exists() {
		let source = std::fs::canonicalize(source_path).map_err(|error| error.to_string())?;
		let image = std::fs::canonicalize(image_path).map_err(|error| error.to_string())?;
		if source == image {
			return Err("source and destination paths are the same".to_owned());
		}
	}

	if image_path.starts_with(source_path) {
		return Err("destination path is contained by source path".to_owned());
	}

	let image = File::options()
		.write(true)
		.truncate(true)
		.create(true)
		.open(image_path)
		.map_err(|error| error.to_string())?;
	let mut image = std::io::BufWriter::with_capacity(1024 * 1024, image);

	if metadata.is_dir() {
		let mut filesystem = StdFilesystem::create(source_path);
		create_xdvdfs_image(&mut filesystem, &mut image, NoOpProgressVisitor)
			.map_err(|error| error.to_string())?;
	} else if metadata.is_file() {
		let source = OffsetWrapper::new(BufReader::new(
			File::open(source_path).map_err(|error| error.to_string())?,
		))
		.map_err(|error| error.to_string())?;
		let mut filesystem = XDVDFSFilesystem::<_, _, StdIOCopier<_, _>>::new(source)
			.ok_or_else(|| "failed to create XDVDFS filesystem".to_owned())?;
		create_xdvdfs_image(&mut filesystem, &mut image, NoOpProgressVisitor)
			.map_err(|error| error.to_string())?;
	} else {
		return Err("source is neither a file nor a directory".to_owned());
	}

	Ok(())
}

fn unpack(image_path: &Path, destination: &Path) -> Result<(), String> {
	let image = File::open(image_path).map_err(|error| error.to_string())?;
	let mut image = OffsetWrapper::new(BufReader::new(image)).map_err(|error| error.to_string())?;
	let volume = read_volume(&mut image).map_err(|error| error.to_string())?;
	let tree = volume
		.root_table
		.file_tree(&mut image)
		.map_err(|error| error.to_string())?;

	std::fs::create_dir_all(destination).map_err(|error| error.to_string())?;
	for (parent, entry) in tree {
		let parent = parent.trim_start_matches('/');
		let directory = destination.join(parent);
		let name = entry.name_str().map_err(|error| error.to_string())?;
		let path = directory.join(name.as_ref());

		if entry.node.dirent.is_directory() {
			std::fs::create_dir_all(path).map_err(|error| error.to_string())?;
			continue;
		}

		std::fs::create_dir_all(&directory).map_err(|error| error.to_string())?;
		let mut output = File::options()
			.write(true)
			.truncate(true)
			.create(true)
			.open(path)
			.map_err(|error| error.to_string())?;
		if entry.node.dirent.is_empty() {
			continue;
		}

		entry
			.node
			.dirent
			.seek_to(&mut image)
			.map_err(|error| error.to_string())?;
		let data = image
			.get_ref()
			.get_ref()
			.try_clone()
			.map_err(|error| error.to_string())?;
		let mut data = BufReader::new(data.take(entry.node.dirent.data.size as u64));
		std::io::copy(&mut data, &mut output).map_err(|error| error.to_string())?;
	}

	Ok(())
}

/// Packs a directory or raw ISO image into an XDVDFS image.
///
/// Paths must be UTF-8. The return value is zero on success. On failure, a
/// null-terminated UTF-8 message is written to error_buffer when provided.
#[no_mangle]
pub unsafe extern "C" fn xdvdfs_pack(
	source_path: *const c_char,
	image_path: *const c_char,
	error_buffer: *mut u8,
	error_capacity: usize,
) -> i32 {
	let result = std::panic::catch_unwind(|| {
		if source_path.is_null() || image_path.is_null() {
			return Err((
				INVALID_ARGUMENT,
				"source_path and image_path are required".to_owned(),
			));
		}

		let source = CStr::from_ptr(source_path)
			.to_str()
			.map_err(|_| (INVALID_ARGUMENT, "source_path is not valid UTF-8".to_owned()))?;
		let image = CStr::from_ptr(image_path)
			.to_str()
			.map_err(|_| (INVALID_ARGUMENT, "image_path is not valid UTF-8".to_owned()))?;

		pack(Path::new(source), Path::new(image)).map_err(|error| (OPERATION_FAILED, error))
	});

	match result {
		Ok(Ok(())) => SUCCESS,
		Ok(Err((code, message))) => {
			write_error(error_buffer, error_capacity, &message);
			code
		}
		Err(_) => {
			write_error(error_buffer, error_capacity, "xdvdfs_pack panicked");
			PANIC
		}
	}
}

/// Extracts an XDVDFS image into a directory.
///
/// Paths must be UTF-8. The return value is zero on success. On failure, a
/// null-terminated UTF-8 message is written to error_buffer when provided.
#[no_mangle]
pub unsafe extern "C" fn xdvdfs_unpack(
	image_path: *const c_char,
	destination_path: *const c_char,
	error_buffer: *mut u8,
	error_capacity: usize,
) -> i32 {
	let result = std::panic::catch_unwind(|| {
		if image_path.is_null() || destination_path.is_null() {
			return Err((
				INVALID_ARGUMENT,
				"image_path and destination_path are required".to_owned(),
			));
		}

		let image = CStr::from_ptr(image_path)
			.to_str()
			.map_err(|_| (INVALID_ARGUMENT, "image_path is not valid UTF-8".to_owned()))?;
		let destination = CStr::from_ptr(destination_path)
			.to_str()
			.map_err(|_| (INVALID_ARGUMENT, "destination_path is not valid UTF-8".to_owned()))?;

		unpack(Path::new(image), Path::new(destination))
			.map_err(|error| (OPERATION_FAILED, error))
	});

	match result {
		Ok(Ok(())) => SUCCESS,
		Ok(Err((code, message))) => {
			write_error(error_buffer, error_capacity, &message);
			code
		}
		Err(_) => {
			write_error(error_buffer, error_capacity, "xdvdfs_unpack panicked");
			PANIC
		}
	}
}

/// Callbacks used by xdvdfs_walk. Every callback returns zero to continue; any
/// other value stops the walk and xdvdfs_walk returns XDVDFS_CANCELLED.
#[repr(C)]
pub struct XdvdfsWalkCallbacks {
	/// Called once before any entry with the total file payload size and file count.
	pub on_start: unsafe extern "C" fn(total_bytes: u64, file_count: u64, ctx: *mut c_void) -> i32,
	/// Called for each directory. The path is relative, UTF-8 and uses '/' separators.
	pub on_directory: unsafe extern "C" fn(path: *const c_char, ctx: *mut c_void) -> i32,
	/// Called before the data of each file. Empty files get no data callbacks.
	pub on_file_begin:
		unsafe extern "C" fn(path: *const c_char, size: u64, ctx: *mut c_void) -> i32,
	/// Called with consecutive chunks of the current file.
	pub on_file_data:
		unsafe extern "C" fn(data: *const u8, length: usize, ctx: *mut c_void) -> i32,
}

const WALK_CHUNK_SIZE: usize = 1024 * 1024;

fn walk(
	image_path: &Path,
	callbacks: &XdvdfsWalkCallbacks,
	ctx: *mut c_void,
) -> Result<(), (i32, String)> {
	let failed = |error: String| (OPERATION_FAILED, error);
	let stopped = |code: i32| {
		if code == 0 {
			Ok(())
		} else {
			Err((CANCELLED, "operation stopped by caller".to_owned()))
		}
	};

	let image = File::open(image_path).map_err(|error| failed(error.to_string()))?;
	let mut image =
		OffsetWrapper::new(BufReader::new(image)).map_err(|error| failed(error.to_string()))?;
	let volume = read_volume(&mut image).map_err(|error| failed(error.to_string()))?;
	let tree = volume
		.root_table
		.file_tree(&mut image)
		.map_err(|error| failed(error.to_string()))?;

	let mut total_bytes = 0u64;
	let mut file_count = 0u64;
	for (_, entry) in &tree {
		if !entry.node.dirent.is_directory() {
			total_bytes += entry.node.dirent.data.size as u64;
			file_count += 1;
		}
	}
	stopped(unsafe { (callbacks.on_start)(total_bytes, file_count, ctx) })?;

	let mut buffer = vec![0u8; WALK_CHUNK_SIZE];
	for (parent, entry) in &tree {
		let name = entry
			.name_str()
			.map_err(|error| failed(error.to_string()))?;
		let parent = parent.trim_start_matches('/');
		let relative = if parent.is_empty() {
			name.to_string()
		} else {
			format!("{}/{}", parent, name)
		};
		let c_path = CString::new(relative)
			.map_err(|_| failed("entry name contains a NUL character".to_owned()))?;

		if entry.node.dirent.is_directory() {
			stopped(unsafe { (callbacks.on_directory)(c_path.as_ptr(), ctx) })?;
			continue;
		}

		let size = entry.node.dirent.data.size as u64;
		stopped(unsafe { (callbacks.on_file_begin)(c_path.as_ptr(), size, ctx) })?;
		if entry.node.dirent.is_empty() || size == 0 {
			continue;
		}

		entry
			.node
			.dirent
			.seek_to(&mut image)
			.map_err(|error| failed(error.to_string()))?;
		let mut data = image
			.get_ref()
			.get_ref()
			.try_clone()
			.map_err(|error| failed(error.to_string()))?;
		let mut remaining = size;
		while remaining > 0 {
			let chunk = remaining.min(buffer.len() as u64) as usize;
			data.read_exact(&mut buffer[..chunk])
				.map_err(|error| failed(format!("failed to read file data: {error}")))?;
			stopped(unsafe { (callbacks.on_file_data)(buffer.as_ptr(), chunk, ctx) })?;
			remaining -= chunk as u64;
		}
	}

	Ok(())
}

/// Streams every directory and file of an XDVDFS image to the supplied callbacks
/// without writing anything to disk. Supports XISO, XGD1, XGD2 and XGD3 layouts.
///
/// image_path is a null-terminated UTF-8 string. Returns XDVDFS_CANCELLED when a
/// callback returns a non-zero value. The error buffer follows the same rules as
/// xdvdfs_pack.
#[no_mangle]
pub unsafe extern "C" fn xdvdfs_walk(
	image_path: *const c_char,
	callbacks: *const XdvdfsWalkCallbacks,
	ctx: *mut c_void,
	error_buffer: *mut u8,
	error_capacity: usize,
) -> i32 {
	let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
		if image_path.is_null() || callbacks.is_null() {
			return Err((
				INVALID_ARGUMENT,
				"image_path and callbacks are required".to_owned(),
			));
		}

		let image = CStr::from_ptr(image_path)
			.to_str()
			.map_err(|_| (INVALID_ARGUMENT, "image_path is not valid UTF-8".to_owned()))?;
		walk(Path::new(image), &*callbacks, ctx)
	}));

	match result {
		Ok(Ok(())) => SUCCESS,
		Ok(Err((code, message))) => {
			write_error(error_buffer, error_capacity, &message);
			code
		}
		Err(_) => {
			write_error(error_buffer, error_capacity, "xdvdfs_walk panicked");
			PANIC
		}
	}
}