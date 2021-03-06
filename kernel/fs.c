/* ------------------------------------------------------------------------ *\
**
**  This file is part of the Chaos Kernel, and is made available under
**  the terms of the GNU General Public License version 2.
**
**  Copyright (C) 2017 - Benjamin Grange <benjamin.grange@epitech.eu>
**
\* ------------------------------------------------------------------------ */

#include <kernel/rwlock.h>
#include <kernel/fs.h>
#include <kernel/bdev.h>
#include <kernel/init.h>
#include <kernel/kalloc.h>
#include <kernel/thread.h>
#include <kernel/vector.h>
#include <stdio.h>

extern struct fs_hook const __start_fs_hook[] __weak;
extern struct fs_hook const __end_fs_hook[] __weak;

struct mount_vector
{
	NEW_VECTOR(struct fs_mount *);
};

static struct mount_vector mounts_vector;
static struct rwlock mounts_lock = RWLOCK_DEFAULT;

/*
** If input is an absolute path, a copy of it is returned.
** If input is a relative path, a concatenation of input and cwd is returned.
*/
static char *
resolve_input(char const *cwd, char const *input)
{
	size_t len;
	char *out;

	if (*input == '/') {
		out = strdup(input);
	} else {
		len = strlen(cwd) + strlen(input) + 2;
		out = kalloc(len * sizeof(char));
		if (unlikely(!out)) {
			return (NULL);
		}
		strcpy(out, cwd);
		strcat(out, "/");
		strcat(out, input);
	}
	return (out);
}

/*
** Resolves the given path, by removing double separators, '.' and '..'.
**
** Inspired by lk's 'fs_normalize_path()', written by geist.
*/
static void
resolve_path(char *path)
{
	int outpos;
	int pos;
	bool done;
	char c;

	enum {
		INITIAL,
		FIELD_START,
		IN_FIELD,
		SEPARATOR,
		SEEN_SEPARATOR,
		DOT,
		SEEN_DOT,
		DOTDOT,
		SEEN_DOTDOT,
	} state = INITIAL;
	pos = 0;
	outpos = 0;
	done = false;

	/* Resolve the given path */
	while (!done)
	{
		c = path[pos];
		switch (state) {
		case INITIAL:
			if (c == '/') {
				state = SEPARATOR;
			}
			else if (c == '.') {
				state = DOT;
			} else {
				state = FIELD_START;
			}
			break;
		case FIELD_START:
			if (c == '.') {
				state = DOT;
			} else if ( c == '\0') {
				done = true;
			} else {
				state = IN_FIELD;
			}
			break;
		case IN_FIELD:
			if (c == '/') {
				state = SEPARATOR;
			} else if (c == '\0') {
				done = true;
			} else {
				path[outpos++] = c;
				++pos;
			}
			break;
		case SEPARATOR:
			path[outpos++] = '/';
			++pos;
			state = SEEN_SEPARATOR;
			break;
		case SEEN_SEPARATOR:
			if (c == '/') {
				++pos;
			} else if (c == '\0') {
				done = true;
			} else {
				state = FIELD_START;
			}
			break;
		case DOT:
			++pos;
			state = SEEN_DOT;
			break;
		case SEEN_DOT:
			if (c == '.') {
				state = DOTDOT;
			} else if (c == '/') { /* Filename == '.', eat it */
				++pos;
				state = SEEN_SEPARATOR;
			} else if (c == '\0') {
				done = true;
			} else { /* Filename starting with '.' */
				path[outpos++] = '.';
				state = IN_FIELD;
			}
			break;
		case DOTDOT:
			++pos;
			state = SEEN_DOTDOT;
			break;
		case SEEN_DOTDOT:
			if (c == '/' || c == '\0') { /* Filename == '..' */
				outpos -= (outpos > 0);
				while (outpos > 0) { /* Walk backward */
					if (path[outpos - 1] == '/') {
						break;
					}
					--outpos;
				}
				++pos;
				state = SEEN_SEPARATOR;
				if (c == '\0') {
					done = true;
				}
			} else { /* Filename starting with '.. */
				path[outpos++] = '.';
				path[outpos++] = '.';
				state = IN_FIELD;
			}
			break;
		}
	}

	/* Remove trailing slash */
	if (outpos == 0) {
		path[outpos++] = '/';
	} else if (outpos > 1 && path[outpos - 1] == '/') {
		--outpos;
	}
	path[outpos] = 0;
}

/*
** Finds the filesystem implementation for the given filesystem name
*/
static struct fs_hook const *
find_fs(char const *name)
{
	struct fs_hook const *hook;

	for (hook = __start_fs_hook; hook != __end_fs_hook; ++hook) {
		if (!strcmp(hook->name, name)) {
			return (hook);
		}
	}
	return (NULL);
}

/*
** Finds the mount structure holding the given path, and stores
** the remaining path in 'trimmed_path'.
**
** Bumps the reference counter before returning.
** The returned struct fs_mount is already locked.
*/
static struct fs_mount *
find_mount(char const *path, char const **trimmed_path)
{
	size_t mount_pathlen;
	struct fs_mount *mount;

	rwlock_acquire_read(&mounts_lock);
	vector_foreach(&mounts_vector, mount_ptr) {
		mount = *mount_ptr;
		mutex_acquire(&mount->lock);
		mount_pathlen = strlen(mount->path);
		if (!strncmp(path, mount->path, mount_pathlen)) {
			*trimmed_path = path + mount_pathlen;
			*trimmed_path += **trimmed_path == '\\';
			++mount->ref_count;
			rwlock_release_read(&mounts_lock);
			return (mount);
		}
		mutex_release(&mount->lock);
	}
	rwlock_release_read(&mounts_lock);
	return (NULL);
}

/*
** Decrements the reference counter of the given mount structure,
** eventually causing a unmount operation if it reaches 0.
**
** The mount must be locked before calling this.
** This will unlock the mount.
*/
static void
put_mount(struct fs_mount *mount)
{
	--mount->ref_count;
	if (mount->ref_count == 0) {
		rwlock_acquire_write(&mounts_lock);
		{
			mount->api->unmount(mount->fs_data);
			kfree(mount->path);
			bdev_close(mount->device);
			vector_remove_value(&mounts_vector, mount);
			kfree(mount);
		}
		rwlock_release_write(&mounts_lock);
	} else {
		mutex_release(&mount->lock);
	}
}

/*
** Mounts the given file system api at the given path for the given device.
*/
static status_t
mount(char const *path, char const *device, struct fs_api *const api)
{
	struct bdev *bdev;
	struct fs_mount *mount;
	char const *relative;
	char *tmp;
	status_t err;

	bdev = NULL;
	mount = NULL;

	if (unlikely(!(tmp = resolve_input(current_thread()->cwd, path)))) {
		return (ERR_NO_MEMORY);
	}
	resolve_path(tmp);

	if ((mount = find_mount(tmp, &relative))) {
		err = ERR_ALREADY_MOUNTED;
		goto err;
	}

	if (!(bdev = bdev_open(device))) {
		err = ERR_NOT_FOUND;
		goto err;
	}

	mount = kalloc(sizeof(*mount));
	if (unlikely(!mount)) {
		err = ERR_NO_MEMORY;
		goto err;
	}
	memset(mount, 0, sizeof(*mount));
	mutex_init(&mount->lock);
	mount->path = tmp;
	mount->device= bdev;
	mount->fs_data = NULL;
	mount->api = api;
	mount->ref_count = 1;
	mutex_acquire(&mount->lock);

	/* Adding the mount to the mount vector */
	rwlock_acquire_write(&mounts_lock);
	if ((err = vector_pushback(&mounts_vector, mount))) {
		rwlock_release_write(&mounts_lock);
		goto err;
	}

	/* Calling the mount api function */
	if ((err = api->mount(bdev, &mount->fs_data))) {
		vector_remove_value(&mounts_vector, mount);
		rwlock_release_write(&mounts_lock);
		goto err;
	}
	rwlock_release_write(&mounts_lock);
	mutex_release(&mount->lock);
	return (OK);

err:
	if (mount) {
		kfree(mount);
	}
	if (bdev) {
		bdev_close(bdev);
	}
	kfree(tmp);
	return (err);
}

/*
** Mounts a filesystem at a given path.
*/
status_t
fs_mount(char const *path, char const *fs_name, char const *device)
{
	struct fs_hook const *hook;

	hook = find_fs(fs_name);
	if (!hook) {
		return (ERR_NOT_FOUND);
	}
	return (mount(path, device, hook->api));
}

/*
** Unmounts a filesystem mounted at the given path.
**
** TODO Recursive unmount
*/
status_t
fs_unmount(char const *path)
{
	char *tmp;
	struct fs_mount *mount;

	tmp = resolve_input(current_thread()->cwd, path);
	if (unlikely(!tmp)) {
		return (ERR_NO_MEMORY);
	}
	resolve_path(tmp);
	mount = find_mount(tmp, NULL);
	kfree(tmp);
	if (!mount) {
		return (ERR_NOT_FOUND);
	}
	put_mount(mount);
	if (mount->ref_count > 1) {
		return (ERR_TARGET_BUSY);
	} else {
		put_mount(mount);
	}
	return (OK);
}

/*
** Opens the file at the given path
*/
status_t
fs_open(char const *path, struct file_handle **file_handle)
{
	struct file_handle *fh;
	struct fs_mount *mount;
	char *tmp;
	char const *newpath;
	status_t err;

	mount = NULL;
	fh = NULL;
	if (unlikely(!(tmp = resolve_input(current_thread()->cwd, path)))) {
		return (ERR_NO_MEMORY);
	}
	resolve_path(tmp);

	if (!(mount = find_mount(tmp, &newpath))) {
		err = ERR_NOT_FOUND;
		goto err;
	}

	if (!(fh = kalloc(sizeof(*fh)))) {
		err = ERR_NO_MEMORY;
		goto err;
	}
	memset(fh, 0, sizeof(*fh));
	fh->mount = mount;

	err = mount->api->open(fh, newpath);
	if (err) {
		goto err;
	}
	mutex_release(&mount->lock);

	*file_handle = fh;
	return (OK);

err:
	kfree(tmp);
	if (mount) {
		put_mount(mount);
	}
	kfree(fh);
	return (err);
}

/*
** Opens a file as a directory
*/
status_t
fs_opendir(struct file_handle *file_handle, struct dir_handle **dir_handle)
{
	struct dir_handle *dh;
	status_t err;
	struct fs_mount *mount;

	mount = file_handle->mount;
	if (!(file_handle->type & FS_DIRECTORY)) {
		return (ERR_NOT_DIRECTORY);
	}

	if (!(dh = kalloc(sizeof(*dh)))) {
		err = ERR_NO_MEMORY;
		goto err;
	}
	memset(dh, 0, sizeof(*dh));
	dh->file_handle = file_handle;

	err = mount->api->opendir(dh);
	if (err) {
		goto err;
	}
	*dir_handle = dh;
	return (OK);

err:
	kfree(dh);
	return (err);
}

/*
** Attemts to read *size bytes to dest.
** On success, *size is set to the amount actually read,
** and the seek offset is updated to the end of the read area.
** On failure, *size is set to the amount that was actually attempted to be read.
*/
status_t
fs_read(struct file_handle *file_handle, void *dest, size_t *size)
{
	struct fs_mount *mount;

	mount = file_handle->mount;
	if (!(file_handle->type & FS_REGULAR_FILE)) {
		return (ERR_BAD_HANDLER);
	}
	return (mount->api->read(file_handle, dest, size));
}

/*
** Tries to set the seek offset to the specified offset
** Returns the offset after completition (may be different on EOF)
*/
size_t
fs_seek(struct file_handle *file_handle, size_t offset)
{
	struct fs_mount *mount;

	mount = file_handle->mount;
	if (!(file_handle->type & FS_REGULAR_FILE)) {
		return (ERR_BAD_HANDLER);
	}
	return (mount->api->seek(file_handle, offset));
}

/*
** Closes the given handle, and kfrees it.
** The file handle is closed and freed in all cases.
** Errors indicate failure to *commit* not to *close*.
*/
status_t
fs_close(struct file_handle *file_handle)
{
	status_t err;
	struct fs_mount *mount;

	mount = file_handle->mount;
	err = mount->api->close(file_handle);
	put_mount(mount);
	kfree(file_handle);
	return (err);
}

/*
** Closes the given directory handle, and kfrees it.
** Closing the directory itself can't fail, closing the file handle can.
*/
status_t
fs_closedir(struct dir_handle *dir_handle)
{
	status_t err;
	struct fs_mount *mount;

	if (!(dir_handle->file_handle->type & FS_DIRECTORY)) {
		return (ERR_BAD_HANDLER);
	}
	mount = dir_handle->file_handle->mount;
	mount->api->closedir(dir_handle);
	err = fs_close(dir_handle->file_handle);
	kfree(dir_handle);
	return (err);
}

/*
** Reads an entry in the given directory, and stores it in 'dirent'.
** Returns ERR_END_OF_DIRECTORY when there is nothing left to read.
** Dirent->name is null-terminated.
*/
status_t
fs_readdir(struct dir_handle *dir_handle, struct dirent *dirent)
{
	struct fs_mount *mount;

	if (!(dir_handle->file_handle->type & FS_DIRECTORY)) {
		return (ERR_BAD_HANDLER);
	}
	mount = dir_handle->file_handle->mount;
	return (mount->api->readdir(dir_handle, dirent));
}

/*
** Initialises file system
** Runs some tests (removable).
*/
static void
init_fs(void)
{
	/* Mount initrd on root */
	assert_eq(fs_mount("/", "dumbfs", "initrd"), OK);
	printf("Filesystem 'dumbfs' mounted on '/'.\n");
}

NEW_INIT_HOOK(filesystem, &init_fs, INIT_LEVEL_FILESYSTEM);
