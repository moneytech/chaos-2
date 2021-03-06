/* ------------------------------------------------------------------------ *\
**
**  This file is part of the Chaos Kernel, and is made available under
**  the terms of the GNU General Public License version 2.
**
**  Copyright (C) 2017 - Benjamin Grange <benjamin.grange@epitech.eu>
**
\* ------------------------------------------------------------------------ */

#include <kernel/kalloc.h>
#include <kernel/bdev.h>
#include <lib/fs/dumbfs.h>
#include <string.h>

static status_t
dumbfs_mount(struct bdev *bdev, void **fscookie)
{
	struct fs_dumb *dumb;
	status_t err;

	if (!(dumb = kalloc(sizeof(*dumb)))) {
		return (ERR_NO_MEMORY);
	}

	if (bdev_read(bdev, &dumb->nb_files, 0, sizeof(uint32)) != sizeof(uint32)) {
		err = ERR_BAD_DEVICE;
		goto err;
	}
	dumb->bdev = bdev;
	*fscookie = dumb;
	return (OK);
err:
	kfree(dumb);
	return (err);
}

static status_t
dumbfs_unmount(void *cookie)
{
	kfree(cookie);
	return (OK);
}

static status_t
dumbfs_open(
	struct file_handle *handle,
	char const *path
)
{
	uint32 file_index;
	struct dumbfs_file_entry file_header;
	struct fs_mount *mount;
	struct fs_dumb *dumb;
	uint offset;
	size_t path_len;
	char *file_path;
	status_t err;
	struct dumbfs_file *file;

	err = OK;
	mount = handle->mount;
	dumb = mount->fs_data;
	if (!path[0]) {
		handle->type |= FS_DIRECTORY;
		handle->file_data = NULL;
		return (OK);
	}
	handle->type |= FS_REGULAR_FILE;
	path_len = strlen(path);
	if (!(file_path = kalloc(path_len + 1))) {
		return (ERR_NO_MEMORY);
	}
	file_index = 0;
	offset = sizeof(uint32_t);
	while (file_index < dumb->nb_files) {
		if (bdev_read(mount->device, &file_header, offset, sizeof(file_header)) != sizeof(file_header)
			|| bdev_read(mount->device, file_path, offset + sizeof(file_header), path_len + 1) != (ssize_t)path_len + 1)
		{
			err = ERR_BAD_DEVICE;
			goto end;
		}
		offset += sizeof(struct dumbfs_file_entry);
		if (!strncmp(path, file_path, path_len + 1)) {
			if (!(file = kalloc(sizeof(*file)))) {
				err = ERR_NO_MEMORY;
				goto end;
			}
			memcpy(&file->header, &file_header, sizeof(file->header));
			file->bdev_offset = offset + path_len + 1;
			file->seek_offset = 0;
			handle->file_data = file;
			goto end;
		}
		offset += file_header.entry_size;
		++file_index;
	}
	err = ERR_NOT_FOUND;
end:
	kfree(file_path);
	return (err);
}

static status_t
dumbfs_read(
	struct file_handle *handle,
	void *dest,
	size_t *size
)
{
	struct fs_mount *mount;
	struct dumbfs_file *file;
	ssize_t r;

	mount = handle->mount;
	file = handle->file_data;
	if (file->seek_offset + *size > file->header.file_size)
		*size = file->header.file_size - file->seek_offset;
	r = bdev_read(mount->device, dest, file->bdev_offset + file->seek_offset, *size);
	if (r < 0) {
		return (ERR_BAD_DEVICE);
	}
	*size = r;
	file->seek_offset += *size;
	return (OK);
}

static size_t
dumbfs_seek(
	struct file_handle *handle,
	size_t offset
)
{
	struct dumbfs_file *file;

	file = handle->file_data;
	if (offset > file->header.file_size)
	  offset = file->header.file_size;
	file->seek_offset = offset;
	return offset;
}

static status_t
dumbfs_close(
	struct file_handle *handle
)
{
	kfree(handle->file_data);
	return (OK);
}

static status_t
dumbfs_opendir(
	struct dir_handle *dir_handle
)
{
	struct dumbfs_dir *dir;

	if (!(dir = kalloc(sizeof(*dir)))) {
		return (ERR_NO_MEMORY);
	}
	dir->bdev_offset = sizeof(uint);
	dir->file_index = 0;
	dir_handle->dir_data = dir;
	return (OK);
}

static status_t
dumbfs_readdir(
	struct dir_handle *dir_handle,
	struct dirent *dirent
)
{
	struct dumbfs_dir *dir;
	struct fs_mount *mount;
	struct fs_dumb *dumb;
	struct dumbfs_file_entry file_header;
	ssize_t r;

	mount = dir_handle->file_handle->mount;
	dir = dir_handle->dir_data;
	dumb = mount->fs_data;
	if (dir->file_index >= dumb->nb_files) {
		return (ERR_END_OF_DIRECTORY);
	}
	dirent->dir = false;
	if (bdev_read(mount->device, &file_header, dir->bdev_offset, sizeof(file_header)) != sizeof(file_header)) {
		return (ERR_BAD_DEVICE);
	}
	r = bdev_read(mount->device, dirent->name, dir->bdev_offset + sizeof(file_header), sizeof(dirent->name) - 1);
	if (r < 0) {
		return (ERR_BAD_DEVICE);
	}
	dirent->name[r] = 0;
	dir->bdev_offset += sizeof(struct dumbfs_file_entry) + file_header.entry_size;
	++dir->file_index;
	return (OK);
}

static void
dumbfs_closedir(
	struct dir_handle *dir_handle
)
{
	kfree(dir_handle->dir_data);
}

static struct fs_api dumbfs_api =
{
	.mount = &dumbfs_mount,
	.unmount = &dumbfs_unmount,
	.open = &dumbfs_open,
	.read = &dumbfs_read,
	.seek = &dumbfs_seek,
	.close = &dumbfs_close,
	.opendir = &dumbfs_opendir,
	.readdir = &dumbfs_readdir,
	.closedir = &dumbfs_closedir,
};

NEW_FILESYSTEM(dumbfs, &dumbfs_api);
