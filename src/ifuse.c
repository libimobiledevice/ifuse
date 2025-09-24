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

#define FUSE_USE_VERSION  30

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
#define HOUSE_ARREST_SERVICE_NAME "com.apple.mobile.house_arrest"

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/house_arrest.h>
#include <libimobiledevice/installation_proxy.h>

/* FreeBSD and others don't have ENODATA, so let's fake it */
#ifndef ENODATA
#define ENODATA EIO
#endif

house_arrest_client_t house_arrest = NULL;

/* assume this is the default block size */
int g_blocksize = 4096;

idevice_t device = NULL;
lockdownd_client_t control = NULL;

int debug = 0;

static struct {
	char *mount_point;
	char *device_udid;
	char *appid;
	int use_container;
	int should_list_apps;
	char *service_name;
	lockdownd_service_descriptor_t service;
	int use_network;
} opts;

enum {
	KEY_HELP = 1,
	KEY_VERSION,
	KEY_ROOT,
	KEY_UDID,
	KEY_UDID_LONG,
	KEY_NETWORK,
	KEY_NETWORK_LONG,
	KEY_VENDOR_DOCUMENTS_LONG,
	KEY_VENDOR_CONTAINER_LONG,
	KEY_LIST_APPS_LONG,
	KEY_DEBUG,
	KEY_DEBUG_LONG
};

static struct fuse_opt ifuse_opts[] = {
	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_KEY("-h",             KEY_HELP),
	FUSE_OPT_KEY("--help",         KEY_HELP),
	FUSE_OPT_KEY("-u %s",          KEY_UDID),
	FUSE_OPT_KEY("--udid %s",      KEY_UDID_LONG),
	FUSE_OPT_KEY("-n",             KEY_NETWORK),
	FUSE_OPT_KEY("--network",      KEY_NETWORK_LONG),
	FUSE_OPT_KEY("--root",         KEY_ROOT),
	FUSE_OPT_KEY("-d",             KEY_DEBUG),
	FUSE_OPT_KEY("--debug",        KEY_DEBUG_LONG),
	FUSE_OPT_KEY("--documents %s", KEY_VENDOR_DOCUMENTS_LONG),
	FUSE_OPT_KEY("--container %s", KEY_VENDOR_CONTAINER_LONG),
	FUSE_OPT_KEY("--list-apps",    KEY_LIST_APPS_LONG),
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
	{AFC_E_SUCCESS               , 0},
	{AFC_E_OP_HEADER_INVALID     , EIO},
	{AFC_E_NO_RESOURCES          , EMFILE},
	{AFC_E_READ_ERROR            , ENOTDIR},
	{AFC_E_WRITE_ERROR           , EIO},
	{AFC_E_UNKNOWN_PACKET_TYPE   , EIO},
	{AFC_E_INVALID_ARG           , EINVAL},
	{AFC_E_OBJECT_NOT_FOUND      , ENOENT},
	{AFC_E_OBJECT_IS_DIR         , EISDIR},
	{AFC_E_DIR_NOT_EMPTY         , ENOTEMPTY},
	{AFC_E_PERM_DENIED           , EPERM},
	{AFC_E_SERVICE_NOT_CONNECTED , ENXIO},
	{AFC_E_OP_TIMEOUT            , ETIMEDOUT},
	{AFC_E_TOO_MUCH_DATA         , EFBIG},
	{AFC_E_END_OF_DATA           , ENODATA},
	{AFC_E_OP_NOT_SUPPORTED      , ENOSYS},
	{AFC_E_OBJECT_EXISTS         , EEXIST},
	{AFC_E_OBJECT_BUSY           , EBUSY},
	{AFC_E_NO_SPACE_LEFT         , ENOSPC},
	{AFC_E_OP_WOULD_BLOCK        , EWOULDBLOCK},
	{AFC_E_IO_ERROR              , EIO},
	{AFC_E_OP_INTERRUPTED        , EINTR},
	{AFC_E_OP_IN_PROGRESS        , EALREADY},
	{AFC_E_INTERNAL_ERROR        , EIO},
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

static int ifuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
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
#ifdef _DARWIN_FEATURE_64_BIT_INODE
			else if (!strcmp(info[i], "st_birthtime")) { /* available on iOS 7+ */
				stbuf->st_birthtime = (time_t)(atoll(info[i+1]) / 1000000000);
			}
#endif
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

static int ifuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	int i;
	char **dirs = NULL;
	afc_client_t afc = fuse_get_context()->private_data;

	afc_read_directory(afc, path, &dirs);

	if (!dirs)
		return -ENOENT;

	for (i = 0; dirs[i]; i++) {
		filler(buf, dirs[i], NULL, 0, 0);
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

static int ifuse_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi)
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

void *ifuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	afc_client_t afc = NULL;

	conn->want &= FUSE_CAP_ASYNC_READ;

	if (house_arrest) {
		afc_client_new_from_house_arrest_client(house_arrest, &afc);
	} else {
		afc_client_new(device, opts.service, &afc);
	}

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
	idevice_free(device);
}

int ifuse_flush(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

int ifuse_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
	return 0;
}

int ifuse_chown(const char *file, uid_t user, gid_t group, struct fuse_file_info *fi) {
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

int ifuse_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	afc_client_t afc = fuse_get_context()->private_data;
	afc_error_t err = afc_truncate(afc, path, size);
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

int ifuse_rename(const char *from, const char *to, unsigned int flags)
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
	.readlink = ifuse_readlink,
	.symlink = ifuse_symlink,
	.link = ifuse_link,
	.unlink = ifuse_unlink,
	.rename = ifuse_rename,
	.utimens = ifuse_utimens,
	.fsync = ifuse_fsync,
	.release = ifuse_release,
	.init = ifuse_init,
	.destroy = ifuse_cleanup,
	.chmod = ifuse_chmod,
	.chown = ifuse_chown,
};

static void print_usage()
{
	fprintf(stderr, "Usage: " PACKAGE_NAME " MOUNTPOINT [OPTIONS]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Mount directories of an iOS device locally using fuse.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "OPTIONS:\n");
	fprintf(stderr, "  -o opt,[opt...]\tmount options\n");
	fprintf(stderr, "  -u, --udid UDID\tmount specific device by UDID\n");
	fprintf(stderr, "  -n, --network\t\tconnect to network device\n");
	fprintf(stderr, "  -h, --help\t\tprint usage information\n");
	fprintf(stderr, "  -V, --version\t\tprint version\n");
	fprintf(stderr, "  -d, --debug\t\tenable libimobiledevice communication debugging\n");
	fprintf(stderr, "  --documents APPID\tmount 'Documents' folder of app identified by APPID\n");
	fprintf(stderr, "  --container APPID\tmount sandbox root of an app identified by APPID\n");
	fprintf(stderr, "  --list-apps\t\tlist installed apps that have file sharing enabled\n");
	fprintf(stderr, "  --root\t\tmount root file system (jailbroken device required)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  $ ifuse /media/iPhone --root\n\n");
	fprintf(stderr, "  This mounts the root filesystem of the first attached device on\n");
	fprintf(stderr, "  this computer in the directory /media/iPhone.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Homepage:    <" PACKAGE_URL ">\n");
	fprintf(stderr, "Bug Reports: <" PACKAGE_BUGREPORT ">\n");
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
	case KEY_NETWORK:
	case KEY_NETWORK_LONG:
		opts.use_network = 1;
		res = 0;
		break;
	case KEY_VENDOR_CONTAINER_LONG:
		opts.use_container = 1;
		opts.appid = strdup(arg+11);
		opts.service_name = HOUSE_ARREST_SERVICE_NAME;
		res = 0;
		break;
	case KEY_VENDOR_DOCUMENTS_LONG:
		opts.appid = strdup(arg+11);
		opts.service_name = HOUSE_ARREST_SERVICE_NAME;
		res = 0;
		break;
	case KEY_DEBUG:
	case KEY_DEBUG_LONG:
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
		fprintf(stderr, "%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		exit(EXIT_SUCCESS);
	case KEY_LIST_APPS_LONG:
		opts.should_list_apps = 1;
		break;
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

static void list_available_apps(idevice_t dev)
{
	instproxy_client_t ip = NULL;
	if (instproxy_client_start_service(dev, &ip, "ifuse") != INSTPROXY_E_SUCCESS) {
		fprintf(stderr, "ERROR: Couldn't connect to installation proxy on device\n");
		goto leave_cleanup;
	}

	plist_t client_opts = instproxy_client_options_new();
	instproxy_client_options_add(client_opts, "ApplicationType", "Any", NULL);
	instproxy_client_options_set_return_attributes(client_opts,
				"CFBundleIdentifier",
				"CFBundleDisplayName",
				"CFBundleVersion",
				"UIFileSharingEnabled",
				NULL
	);

	plist_t apps = NULL;
	instproxy_browse(ip, client_opts, &apps);

	if (!apps || (plist_get_node_type(apps) != PLIST_ARRAY)) {
		fprintf(stderr, "ERROR: instproxy_browse returned an invalid plist?!\n");
		goto leave_cleanup;
	}

	/* output colum titles */
	printf("\"%s\",\"%s\",\"%s\"\n", "CFBundleIdentifier", "CFBundleVersion", "CFBundleDisplayName");

	/* output rows with app information */
	uint32_t i = 0;
	for (i = 0; i < plist_array_get_size(apps); i++) {
		plist_t node = plist_array_get_item(apps, i);
		if (node && plist_get_node_type(node) == PLIST_DICT) {
			uint8_t sharing_enabled = 0;
			plist_t val = plist_dict_get_item(node, "UIFileSharingEnabled");
			if (val && plist_get_node_type(val) == PLIST_BOOLEAN) {
				plist_get_bool_val(val, &sharing_enabled);
			}
			if (sharing_enabled) {
				char *bid = NULL;
				char *ver = NULL;
				char *name = NULL;
				val = plist_dict_get_item(node, "CFBundleIdentifier");
				if (val) {
					plist_get_string_val(val, &bid);
				}
				val = plist_dict_get_item(node, "CFBundleVersion");
				if (val) {
					plist_get_string_val(val, &ver);
				}
				val = plist_dict_get_item(node, "CFBundleDisplayName");
				if (val) {
					plist_get_string_val(val, &name);
				}
				printf("\"%s\",\"%s\",\"%s\"\n", bid, ver, name);
				free(bid);
				free(ver);
				free(name);
			}
		}
	}
	plist_free(apps);

leave_cleanup:
	instproxy_client_free(ip);
	idevice_free(dev);
}

int main(int argc, char *argv[])
{
	int res = EXIT_FAILURE;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct stat mst;
	idevice_error_t err = IDEVICE_E_UNKNOWN_ERROR;
	lockdownd_error_t ret = LOCKDOWN_E_UNKNOWN_ERROR;
	enum idevice_options lookup_opts = IDEVICE_LOOKUP_USBMUX | IDEVICE_LOOKUP_NETWORK;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = AFC_SERVICE_NAME;

	if (fuse_opt_parse(&args, NULL, ifuse_opts, ifuse_opt_proc) == -1) {
		return EXIT_FAILURE;
	}

	if (opts.device_udid && !*opts.device_udid) {
		fprintf(stderr, "ERROR: UDID must not be empty\n");
		return EXIT_FAILURE;
	}

	if (!opts.should_list_apps) {
		if (!opts.mount_point) {
			fprintf(stderr, "ERROR: No mount point specified\n");
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
	}

	err = idevice_new_with_options(&device, opts.device_udid, (opts.use_network) ? IDEVICE_LOOKUP_NETWORK : IDEVICE_LOOKUP_USBMUX);
	if (err != IDEVICE_E_SUCCESS) {
		if (opts.device_udid) {
			printf("ERROR: Device %s not found!\n", opts.device_udid);
		} else {
			printf("ERROR: No device found!\n");
		}
		fprintf(stderr, "Is the device properly connected?\n");
		fprintf(stderr, "If it is make sure that your user has permissions to access the raw USB device.\n");
		fprintf(stderr, "If you're still having issues try unplugging the device and reconnecting it.\n");
		return EXIT_FAILURE;
	}

	if (!device) {
		return EXIT_FAILURE;
	}

	if (opts.should_list_apps) {
		list_available_apps(device);
		return EXIT_SUCCESS;
	}

	ret = lockdownd_client_new_with_handshake(device, &control, "ifuse");
	if (ret != LOCKDOWN_E_SUCCESS) {
		idevice_free(device);
		if (ret == LOCKDOWN_E_PASSWORD_PROTECTED) {
			fprintf(stderr, "Please disable the password protection on your device and try again.\n");
			fprintf(stderr, "The device does not allow pairing as long as a password has been set.\n");
			fprintf(stderr, "You can enable it again after the connection succeeded.\n");
#ifdef LOCKDOWN_E_PAIRING_DIALOG_PENDING
		} else if (ret == LOCKDOWN_E_PAIRING_DIALOG_PENDING) {
			fprintf(stderr, "Please dismiss the trust dialog on your device and try again.\n");
			fprintf(stderr, "The device does not allow pairing as long as the dialog has not been accepted.\n");
#endif
		} else {
			fprintf(stderr, "Failed to connect to lockdownd service on the device.\n");
			fprintf(stderr, "Try again. If it still fails try rebooting your device.\n");
		}
		return EXIT_FAILURE;
	}

	if ((lockdownd_start_service(control, opts.service_name, &opts.service) != LOCKDOWN_E_SUCCESS) || !opts.service) {
		lockdownd_client_free(control);
		idevice_free(device);
		fprintf(stderr, "Failed to start AFC service '%s' on the device.\n", opts.service_name);
		if (!strcmp(opts.service_name, AFC2_SERVICE_NAME)) {
			fprintf(stderr, "This service enables access to the root filesystem of your device.\n");
			fprintf(stderr, "Your device needs to be jailbroken and have the AFC2 service installed.\n");
		}
		return EXIT_FAILURE;
	}

	if (!strcmp(opts.service_name, HOUSE_ARREST_SERVICE_NAME)) {
		house_arrest_client_new(device, opts.service, &house_arrest);
		if (!house_arrest) {
			fprintf(stderr, "Could not start document sharing service!\n");
			return EXIT_FAILURE;
		}

		/* FIXME: iOS 3.x house_arrest does not know about VendDocuments yet, thus use VendContainer and chroot manually with fuse subdir module */
		if (house_arrest_send_command(house_arrest, opts.use_container ? "VendContainer": "VendDocuments", opts.appid) != HOUSE_ARREST_E_SUCCESS) {
			fprintf(stderr, "Could not send house_arrest command!\n");
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
			if (str && !strcmp(str, "InstallationLookupFailed")) {
				fprintf(stderr, "The App '%s' is either not present on the device, or the 'UIFileSharingEnabled' key is not set in its Info.plist. Starting with iOS 8.3 this key is mandatory to allow access to an app's Documents folder.\n", opts.appid);
			}
			free(str);
			goto leave_err;
		}
		plist_free(dict);

		if (opts.use_container == 0) {
			fuse_opt_add_arg(&args, "-omodules=subdir");
			fuse_opt_add_arg(&args, "-osubdir=Documents");
		}
	}
	res = fuse_main(args.argc, args.argv, &ifuse_oper, NULL);

leave_err:
	if (house_arrest) {
		house_arrest_client_free(house_arrest);
	}
	return res;
}
