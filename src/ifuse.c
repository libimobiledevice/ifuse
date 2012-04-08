/* 
 * ifuse.c
 * A Fuse filesystem which exposes the iPhone's filesystem.
 *
 * Copyright (c) 2008 Matt Colyer All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */

#define FUSE_USE_VERSION  26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

#define AFC_SERVICE_NAME "com.apple.afc"
#define AFC2_SERVICE_NAME "com.apple.afc2"
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
#define HOUSE_ARREST_SERVICE_NAME "com.apple.mobile.house_arrest"
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/afc.h>
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
#include <libimobiledevice/house_arrest.h>
#endif

/* FreeBSD and others don't have ENODATA, so let's fake it */
#ifndef ENODATA
#define ENODATA EIO
#endif

#ifdef HAVE_LIBIMOBILEDEVICE_1_1
house_arrest_client_t house_arrest = NULL;
#endif

/* assume this is the default block size */
int g_blocksize = 4096;

idevice_t phone = NULL;
lockdownd_client_t control = NULL;

int debug = 0;

static struct {
	char *mount_point;
	char *device_udid;
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	char *appid;
#endif
	char *service_name;
	uint16_t port;
} opts;

enum {
	KEY_HELP = 1,
	KEY_VERSION,
	KEY_ROOT,
	KEY_UDID,
	KEY_UDID_LONG,
	KEY_APPID,
	KEY_APPID_LONG,
	KEY_DEBUG
};

static struct fuse_opt ifuse_opts[] = {
	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_KEY("-h",             KEY_HELP),
	FUSE_OPT_KEY("--help",         KEY_HELP),
	FUSE_OPT_KEY("-u %s",          KEY_UDID),
	FUSE_OPT_KEY("--udid %s",      KEY_UDID_LONG),
	FUSE_OPT_KEY("--root",         KEY_ROOT),
	FUSE_OPT_KEY("--debug",        KEY_DEBUG),
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	FUSE_OPT_KEY("-a %s",          KEY_APPID),
	FUSE_OPT_KEY("--appid %s",     KEY_APPID_LONG),
#endif
	FUSE_OPT_END
};

static void free_dictionary(char **dictionary)
{
	int i = 0;

	if (!dictionary)
		return;

	for (i = 0; dictionary[i]; i++) {
		free(dictionary[i]);
	}
	free(dictionary);
}

struct afc_error_mapping {
	afc_error_t from;
	int to;
} static afc_error_to_errno_map[] = {
	{AFC_E_SUCCESS			, 0},
	{AFC_E_OP_HEADER_INVALID	, EIO},
	{AFC_E_NO_RESOURCES		, EMFILE},
	{AFC_E_READ_ERROR		, ENOTDIR},
	{AFC_E_WRITE_ERROR		, EIO},
	{AFC_E_UNKNOWN_PACKET_TYPE	, EIO},
	{AFC_E_INVALID_ARG		, EINVAL},
	{AFC_E_OBJECT_NOT_FOUND		, ENOENT},
	{AFC_E_OBJECT_IS_DIR		, EISDIR},
	{AFC_E_DIR_NOT_EMPTY		, ENOTEMPTY},
	{AFC_E_PERM_DENIED		, EPERM},
	{AFC_E_SERVICE_NOT_CONNECTED	, ENXIO},
	{AFC_E_OP_TIMEOUT		, ETIMEDOUT},
	{AFC_E_TOO_MUCH_DATA		, EFBIG},
	{AFC_E_END_OF_DATA		, ENODATA},
	{AFC_E_OP_NOT_SUPPORTED		, ENOSYS},
	{AFC_E_OBJECT_EXISTS		, EEXIST},
	{AFC_E_OBJECT_BUSY		, EBUSY},
	{AFC_E_NO_SPACE_LEFT		, ENOSPC},
	{AFC_E_OP_WOULD_BLOCK		, EWOULDBLOCK},
	{AFC_E_IO_ERROR			, EIO},
	{AFC_E_OP_INTERRUPTED		, EINTR},
	{AFC_E_OP_IN_PROGRESS		, EALREADY},
	{AFC_E_INTERNAL_ERROR		, EIO},
	{-1}
};

/** 
 * Tries to convert the AFC error value into a meaningful errno value.
 *
 * @param client AFC client to retrieve status value from.
 *
 * @return errno value.
 */
static int get_afc_error_as_errno(afc_error_t error)
{
	int i = 0;
	int res = -1;

	while (afc_error_to_errno_map[i++].from != -1) {
		if (afc_error_to_errno_map[i].from == error) {
			res = afc_error_to_errno_map[i++].to;
			break;
		}
	}

	if (res == -1) {
		fprintf(stderr, "Unknown AFC status %d.\n", error);
		res = EIO;
	}

	return res;
}

static int get_afc_file_mode(afc_file_mode_t *afc_mode, int flags)
{
	switch (flags & O_ACCMODE) {
		case O_RDONLY:
			*afc_mode = AFC_FOPEN_RDONLY;
			break;
		case O_WRONLY:
			if ((flags & O_TRUNC) == O_TRUNC) {
				*afc_mode = AFC_FOPEN_WRONLY;
			} else if ((flags & O_APPEND) == O_APPEND) {
				*afc_mode = AFC_FOPEN_APPEND;
			} else {
				*afc_mode = AFC_FOPEN_RW;
			}
			break;
		case O_RDWR:
			if ((flags & O_TRUNC) == O_TRUNC) {
				*afc_mode = AFC_FOPEN_WR;
			} else if ((flags & O_APPEND) == O_APPEND) {
				*afc_mode = AFC_FOPEN_RDAPPEND;
			} else {
				*afc_mode = AFC_FOPEN_RW;
			}
			break;
		default:
			*afc_mode = 0;
			return -1;
	}
	return 0;
}

static int ifuse_getattr(const char *path, struct stat *stbuf)
{
	int i;
	int res = 0;
	char **info = NULL;

	afc_client_t afc = fuse_get_context()->private_data;
	afc_error_t ret = afc_get_file_info(afc, path, &info);

	memset(stbuf, 0, sizeof(struct stat));
	if (ret != AFC_E_SUCCESS) {
		int e = get_afc_error_as_errno(ret);
		res = -e;
	} else if (!info) {
		res = -1;
	} else {
		// get file attributes from info list
		for (i = 0; info[i]; i += 2) {
			if (!strcmp(info[i], "st_size")) {
				stbuf->st_size = atoll(info[i+1]);
			} else if (!strcmp(info[i], "st_blocks")) {
				stbuf->st_blocks = atoi(info[i+1]);
			} else if (!strcmp(info[i], "st_ifmt")) {
				if (!strcmp(info[i+1], "S_IFREG")) {
					stbuf->st_mode = S_IFREG;
				} else if (!strcmp(info[i+1], "S_IFDIR")) {
					stbuf->st_mode = S_IFDIR;
				} else if (!strcmp(info[i+1], "S_IFLNK")) {
					stbuf->st_mode = S_IFLNK;
				} else if (!strcmp(info[i+1], "S_IFBLK")) {
					stbuf->st_mode = S_IFBLK;
				} else if (!strcmp(info[i+1], "S_IFCHR")) {
					stbuf->st_mode = S_IFCHR;
				} else if (!strcmp(info[i+1], "S_IFIFO")) {
					stbuf->st_mode = S_IFIFO;
				} else if (!strcmp(info[i+1], "S_IFSOCK")) {
					stbuf->st_mode = S_IFSOCK;
				}
			} else if (!strcmp(info[i], "st_nlink")) {
				stbuf->st_nlink = atoi(info[i+1]);
			} else if (!strcmp(info[i], "st_mtime")) {
				stbuf->st_mtime = (time_t)(atoll(info[i+1]) / 1000000000);
			}
		}
		free_dictionary(info);

		// set permission bits according to the file type
		if (S_ISDIR(stbuf->st_mode)) {
			stbuf->st_mode |= 0755;
		} else if (S_ISLNK(stbuf->st_mode)) {
			stbuf->st_mode |= 0777;
		} else {
			stbuf->st_mode |= 0644;
		}

		// and set some additional info
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();

		stbuf->st_blksize = g_blocksize;
	}

	return res;
}

static int ifuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	int i;
	char **dirs = NULL;
	afc_client_t afc = fuse_get_context()->private_data;

	afc_read_directory(afc, path, &dirs);

	if (!dirs)
		return -ENOENT;

	for (i = 0; dirs[i]; i++) {
		filler(buf, dirs[i], NULL, 0);
	}

	free_dictionary(dirs);

	return 0;
}

static int ifuse_open(const char *path, struct fuse_file_info *fi)
{
	afc_client_t afc = fuse_get_context()->private_data;
	afc_error_t err;
	afc_file_mode_t mode = 0;

	err = get_afc_file_mode(&mode, fi->flags);
	if (err != AFC_E_SUCCESS || (mode == 0)) {
		return -EPERM;
	}

	err = afc_file_open(afc, path, mode, &fi->fh);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	return 0;
}

static int ifuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	return ifuse_open(path, fi);
}

static int ifuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	uint32_t bytes = 0;
	afc_client_t afc = fuse_get_context()->private_data;

	if (size == 0)
		return 0;

	afc_error_t err = afc_file_seek(afc, fi->fh, offset, SEEK_SET);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	err = afc_file_read(afc, fi->fh, buf, size, &bytes);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	return bytes;
}

static int ifuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	uint32_t bytes = 0;
	afc_client_t afc = fuse_get_context()->private_data;

	if (size == 0)
		return 0;

	afc_error_t err = afc_file_seek(afc, fi->fh, offset, SEEK_SET);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	err = afc_file_write(afc, fi->fh, buf, size, &bytes);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	return bytes;
}

static int ifuse_utimens(const char *path, const struct timespec tv[2])
{
	afc_client_t afc = fuse_get_context()->private_data;
	uint64_t mtime = (uint64_t)tv[1].tv_sec * (uint64_t)1000000000 + (uint64_t)tv[1].tv_nsec;

	afc_error_t err = afc_set_file_time(afc, path, mtime);
	if (err == AFC_E_UNKNOWN_PACKET_TYPE) {
		/* ignore error for pre-3.1 devices as they do not support setting file modification times */
		return 0;
	}
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}

	return 0;
}

static int ifuse_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	return 0;
}

static int ifuse_release(const char *path, struct fuse_file_info *fi)
{
	afc_client_t afc = fuse_get_context()->private_data;

	afc_file_close(afc, fi->fh);

	return 0;
}

void *ifuse_init(struct fuse_conn_info *conn)
{
	afc_client_t afc = NULL;

	conn->async_read = 0;

#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	if (house_arrest) {
		afc_client_new_from_house_arrest_client(house_arrest, &afc);
	} else { 
#endif
		afc_client_new(phone, opts.port, &afc);
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	}
#endif

	lockdownd_client_free(control);
	control = NULL;

	if (afc) {
		// get file system block size
		int i;
		char **info_raw = NULL;
		if ((AFC_E_SUCCESS == afc_get_device_info(afc, &info_raw)) && info_raw) {
			for (i = 0; info_raw[i]; i+=2) {
				if (!strcmp(info_raw[i], "FSBlockSize")) {
					g_blocksize = atoi(info_raw[i + 1]);
					break;
				}
			}
			free_dictionary(info_raw);
		}
	}

	return afc;
}

void ifuse_cleanup(void *data)
{
	afc_client_t afc = (afc_client_t) data;

	afc_client_free(afc);
	if (control) {
		lockdownd_client_free(control);
	}
	idevice_free(phone);
}

int ifuse_flush(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

int ifuse_statfs(const char *path, struct statvfs *stats)
{
	afc_client_t afc = fuse_get_context()->private_data;
	char **info_raw = NULL;
	uint64_t totalspace = 0, freespace = 0;
	int i = 0, blocksize = 0;

	afc_error_t err = afc_get_device_info(afc, &info_raw);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}
	if (!info_raw)
		return -ENOENT;

	for (i = 0; info_raw[i]; i++) {
		if (!strcmp(info_raw[i], "FSTotalBytes")) {
			totalspace = strtoull(info_raw[i + 1], (char **) NULL, 10);
		} else if (!strcmp(info_raw[i], "FSFreeBytes")) {
			freespace = strtoull(info_raw[i + 1], (char **) NULL, 10);
		} else if (!strcmp(info_raw[i], "FSBlockSize")) {
			blocksize = atoi(info_raw[i + 1]);
		}
	}
	free_dictionary(info_raw);

	stats->f_bsize = stats->f_frsize = blocksize;
	stats->f_blocks = totalspace / blocksize;
	stats->f_bfree = stats->f_bavail = freespace / blocksize;
	stats->f_namemax = 255;
	stats->f_files = stats->f_ffree = 1000000000;

	return 0;
}

int ifuse_truncate(const char *path, off_t size)
{
	afc_client_t afc = fuse_get_context()->private_data;
	afc_error_t err = afc_truncate(afc, path, size);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}
	return 0;
}

int ifuse_ftruncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	afc_client_t afc = fuse_get_context()->private_data;

	afc_error_t err = afc_file_truncate(afc, fi->fh, size);
	if (err != AFC_E_SUCCESS) {
		int res = get_afc_error_as_errno(err);
		return -res;
	}
	return 0;
}

int ifuse_readlink(const char *path, char *linktarget, size_t buflen)
{
	int i, ret;
	char **info = NULL;
	if (!path || !linktarget || (buflen == 0)) {
		return -EINVAL;
	}
	linktarget[0] = '\0'; // in case the link target cannot be determined
	afc_client_t afc = fuse_get_context()->private_data;
	afc_error_t err = afc_get_file_info(afc, path, &info);
	if ((err == AFC_E_SUCCESS) && info) {
		ret = -1;
		for (i = 0; info[i]; i+=2) {
			if (!strcmp(info[i], "LinkTarget")) {
				strncpy(linktarget, info[i+1], buflen-1);
				linktarget[buflen-1] = '\0';
				ret = 0;
			}
		}
		free_dictionary(info);
	} else {
		ret = get_afc_error_as_errno(err);
		return -ret;
	}

	return ret;
}

int ifuse_symlink(const char *target, const char *linkname)
{
	afc_client_t afc = fuse_get_context()->private_data;
	
	afc_error_t err = afc_make_link(afc, AFC_SYMLINK, target, linkname);
	if (err == AFC_E_SUCCESS)
		return 0;
	
	return -get_afc_error_as_errno(err);
}

int ifuse_link(const char *target, const char *linkname)
{
	afc_client_t afc = fuse_get_context()->private_data;

	afc_error_t err = afc_make_link(afc, AFC_HARDLINK, target, linkname);
	if (err == AFC_E_SUCCESS)
		return 0;

	return -get_afc_error_as_errno(err);
}

int ifuse_unlink(const char *path)
{
	afc_client_t afc = fuse_get_context()->private_data;

	afc_error_t err = afc_remove_path(afc, path);
	if (err == AFC_E_SUCCESS)
		return 0;

	return -get_afc_error_as_errno(err);
}

int ifuse_rename(const char *from, const char *to)
{
	afc_client_t afc = fuse_get_context()->private_data;
	
	afc_error_t err = afc_rename_path(afc, from, to);
	if (err == AFC_E_SUCCESS)
		return 0;

	return -get_afc_error_as_errno(err);
}

int ifuse_mkdir(const char *dir, mode_t ignored)
{
	afc_client_t afc = fuse_get_context()->private_data;

	afc_error_t err = afc_make_directory(afc, dir);
	if (err == AFC_E_SUCCESS)
		return 0;

	return -get_afc_error_as_errno(err);
}

static struct fuse_operations ifuse_oper = {
	.getattr = ifuse_getattr,
	.statfs = ifuse_statfs,
	.readdir = ifuse_readdir,
	.mkdir = ifuse_mkdir,
	.rmdir = ifuse_unlink,
	.create = ifuse_create,
	.open = ifuse_open,
	.read = ifuse_read,
	.write = ifuse_write,
	.truncate = ifuse_truncate,
	.ftruncate = ifuse_ftruncate,
	.readlink = ifuse_readlink,
	.symlink = ifuse_symlink,
	.link = ifuse_link,
	.unlink = ifuse_unlink,
	.rename = ifuse_rename,
	.utimens = ifuse_utimens,
	.fsync = ifuse_fsync,
	.release = ifuse_release,
	.init = ifuse_init,
	.destroy = ifuse_cleanup
};

static void print_usage()
{
	fprintf(stderr, "Usage: ifuse <mount_point> [OPTIONS]\n");
	fprintf(stderr, "Mount filesystem of an iPhone, iPod Touch, iPad or Apple TV.\n\n");
	fprintf(stderr, "  -o opt,[opt...]\tmount options\n");
	fprintf(stderr, "  -u, --udid UDID\tmount specific device by its 40-digit device UDID\n");
	fprintf(stderr, "  -h, --help\t\tprint usage information\n");
	fprintf(stderr, "  -V, --version\t\tprint version\n");
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	fprintf(stderr, "  --appid APPID\t\tmount 'Documents' folder of app identified by APPID\n");
#endif
	fprintf(stderr, "  --root\t\tmount root file system (jailbroken device required)\n");
	fprintf(stderr, "  --debug\t\tenable libimobiledevice communication debugging\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example:\n\n");
	fprintf(stderr, "  $ ifuse /media/iPhone --root\n\n");
	fprintf(stderr, "  This mounts the root filesystem of the first attached device on\n");
	fprintf(stderr, "  this computer in the directory /media/iPhone.\n");
	fprintf(stderr, "\n");
}

static int ifuse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	static int option_num = 0;
	int res = 1;

	switch (key) {
	case KEY_UDID_LONG:
		opts.device_udid = strdup(arg+6);
		res = 0;
		break;
	case KEY_UDID:
		opts.device_udid = strdup(arg+2);
		res = 0;
		break;
#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	case KEY_APPID_LONG:
		opts.appid = strdup(arg+7);
		opts.service_name = HOUSE_ARREST_SERVICE_NAME;
		res = 0;
		break;
	case KEY_APPID:
		opts.appid = strdup(arg+2);
		opts.service_name = HOUSE_ARREST_SERVICE_NAME;
		res = 0;
		break;
#endif
	case KEY_DEBUG:
		idevice_set_debug_level(1);
		res = 0;
		break;
	case KEY_ROOT:
		opts.service_name = AFC2_SERVICE_NAME;
		res = 0;
		break;
	case KEY_HELP:
		print_usage();
		exit(EXIT_SUCCESS);
	case KEY_VERSION:
		fprintf(stderr, "%s version %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		exit(EXIT_SUCCESS);
	case FUSE_OPT_KEY_OPT:
		/* ignore other options and pass them to fuse_main later */
		break;
	case FUSE_OPT_KEY_NONOPT:
		if(option_num == 0) {
			opts.mount_point = strdup(arg);
		}
		if(option_num == 1) {
			/* compatibility to older style which passed a device file option first */
			free(opts.mount_point);
			opts.mount_point = strdup(arg);
		}
		option_num++;
		break;
	}
	return res;
}

int main(int argc, char *argv[])
{
	int res = EXIT_FAILURE;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct stat mst;
	lockdownd_error_t ret = LOCKDOWN_E_SUCCESS;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = AFC_SERVICE_NAME;

	if (fuse_opt_parse(&args, NULL, ifuse_opts, ifuse_opt_proc) == -1) {
		return EXIT_FAILURE;
	}

	if (!opts.mount_point) {
		fprintf(stderr, "ERROR: No mount point specified\n");
		return EXIT_FAILURE;
	}

	if (opts.device_udid && strlen(opts.device_udid) != 40) {
		fprintf(stderr, "Invalid device UDID specified, length needs to be 40 characters\n");
		return EXIT_FAILURE;
	}

	if (stat(opts.mount_point, &mst) < 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "ERROR: the mount point specified does not exist\n");
			return EXIT_FAILURE;
		}
		
		fprintf(stderr, "There was an error accessing the mount point: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	idevice_new(&phone, opts.device_udid ? opts.device_udid : NULL);
	if (!phone) {
		fprintf(stderr, "No device found, is it connected?\n");
		fprintf(stderr, "If it is make sure that your user has permissions to access the raw usb device.\n");
		fprintf(stderr, "If you're still having issues try unplugging the device and reconnecting it.\n");
		return EXIT_FAILURE;
	}
	
	ret = lockdownd_client_new_with_handshake(phone, &control, "ifuse");
	if (ret != LOCKDOWN_E_SUCCESS) {
		idevice_free(phone);
		if (ret == LOCKDOWN_E_PASSWORD_PROTECTED) {
			fprintf(stderr, "Please disable the password protection on your device and try again.\n");
			fprintf(stderr, "The device does not allow pairing as long as a password has been set.\n");
			fprintf(stderr, "You can enable it again after the connection succeeded.\n");
		} else {
			fprintf(stderr, "Failed to connect to lockdownd service on the device.\n");
			fprintf(stderr, "Try again. If it still fails try rebooting your device.\n");
		}
		return EXIT_FAILURE;
	}

	if ((lockdownd_start_service(control, opts.service_name, &opts.port) != LOCKDOWN_E_SUCCESS) || !opts.port) {
		lockdownd_client_free(control);
		idevice_free(phone);
		fprintf(stderr, "Failed to start AFC service '%s' on the device.\n", opts.service_name);
		if (!strcmp(opts.service_name, AFC2_SERVICE_NAME)) {
			fprintf(stderr, "This service enables access to the root filesystem of your device.\n");
			fprintf(stderr, "Your device needs to be jailbroken and have the AFC2 service installed.\n");
		}
		return EXIT_FAILURE;
	}

#ifdef HAVE_LIBIMOBILEDEVICE_1_1
	if (!strcmp(opts.service_name, HOUSE_ARREST_SERVICE_NAME)) {
		house_arrest_client_new(phone, opts.port, &house_arrest);
		if (!house_arrest) {
			fprintf(stderr, "Could not start document sharing service!\n");
			return EXIT_FAILURE;
		}
		if (house_arrest_send_command(house_arrest, "VendContainer", opts.appid) != HOUSE_ARREST_E_SUCCESS) {
			fprintf(stderr, "Could not send VendContainer command!\n");
			goto leave_err;
		}

		plist_t dict = NULL;
		if (house_arrest_get_result(house_arrest, &dict) != HOUSE_ARREST_E_SUCCESS) {
			fprintf(stderr, "Could not get result from document sharing service!\n");
			goto leave_err;
		}
		plist_t node = plist_dict_get_item(dict, "Error");
		if (node) {
			char *str = NULL;
			plist_get_string_val(node, &str);
			fprintf(stderr, "ERROR: %s\n", str);
			if (str) free(str);
			goto leave_err;
		}
		plist_free(dict);

		fuse_opt_add_arg(&args, "-omodules=subdir");
		fuse_opt_add_arg(&args, "-osubdir=Documents");
	}
#endif
	res = fuse_main(args.argc, args.argv, &ifuse_oper, NULL);

#ifdef HAVE_LIBIMOBILEDEVICE_1_1
leave_err:
	if (house_arrest) {
		house_arrest_client_free(house_arrest);
	}
#endif
	return res;
}
