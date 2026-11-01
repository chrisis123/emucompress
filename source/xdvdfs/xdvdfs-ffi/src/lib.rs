use std::ffi::{c_char, CStr};
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