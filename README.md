# ifuse

## About

A fuse filesystem implementation to access the contents of iOS devices.

## Requirements

Development Packages of:
* libfuse (and the associated kernel modules)
* libimobiledevice
* libplist

Software:
* usbmuxd
* make
* autoheader
* automake
* autoconf
* libtool
* pkg-config
* gcc

## Installation

To compile run:
```bash
./autogen.sh
make
sudo make install
```

## Usage

To mount the media partition from the device run:

```bash
ifuse <mountpoint>
```

**HINT**

If you mount your device as regular user, the system might complain that
the file /etc/fuse.conf is not readable. It means you do not belong to the
'fuse' group (see below).

To unmount as a regular user you must run:

```bash
$ fusermount -u <mountpoint>
```

By default, ifuse (via the AFC protocol) gives access to the '/var/root/Media/'
chroot on the device (containing music/pictures). This is the right and safe
way to access the device. However, if the device has been jailbroken, a full
view of the device's filesystem might be available using the following command
when mounting:

```bash
ifuse --root <mountpoint>
```

Note that only older jailbreak software installed the necessary AFC2 service on
the device to enable root filesystem usage. For instance  blackra1n does not
install it and thus does not enable root filesystem access by default!
Use with care as the AFC protocol was not made to access the root filesystem.

If using libimobiledevice >= 1.1.0, ifuse can also be used with the iTunes
file/document sharing feature. It allows you to exchange files with an
application on the device directly through it's documents folder by specifing
the application identifier like this:

```bash
ifuse --documents <appid> <mountpoint>
```

The following example mounts the documents folder of the VLC app to /mnt:

```bash
ifuse --documents org.videolan.vlc-ios /mnt
```

It is also possible to mount the sandboxed root folder of an application
using the --container parameter:

```bash
ifuse --container <appid> <mountpoint>
```

The <appid> (bundle identifier) of an app can be obtained using:

```bash
ifuse --list-apps
```

Addtional help can be shown using:

```bash
ifuse --help
```

## Setting up FUSE

Note that on some systems, you may have to load the 'fuse' kernel
module first and to ensure that you are a member of the 'fuse' group:

```bash
sudo modprobe fuse
sudo adduser $USER fuse
```

You can check your membership of the 'fuse' group with:

```bash
id | grep fuse && echo yes! || echo not yet...
```

If you have just added yourself, you will need to logout and log back
in for the group change to become visible.

## Who/What/Where?

* Home: https://libimobiledevice.org/
* Code: `git clone https://git.libimobiledevice.org/ifuse.git`
* Code (Mirror): `git clone https://github.com/libimobiledevice/ifuse.git`
* Tickets: https://github.com/libimobiledevice/ifuse/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* Twitter: https://twitter.com/libimobiledev

## Credits

Apple, iPhone, iPod, iPad, Apple TV and iPod Touch are trademarks of Apple Inc.

ifuse is an independent software application and has not been authorized,
sponsored, or otherwise approved by Apple Inc.

README Updated on: 2020-06-08
