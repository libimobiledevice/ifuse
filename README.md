# ifuse

*A fuse filesystem implementation to access the contents of iOS devices.*

## Features

This project allows mounting various directories of an iOS device locally using
the [FUSE file system interface](https://github.com/libfuse/libfuse).

Some key features are:

- **Media**: Mount media directory of an iOS device locally
- **Apps**: Mount sandbox container or document directory of an app
- **Jailbreak**: Mount root filesystem on jailbroken devices *(requires AFC2 service)*
- **Browse**: Allows to retrieve a list of installed file-sharing enabled apps
- **Implementation**: Uses [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice) for communication with the device

## Installation / Getting started

Please note that `usbmuxd` must also be installed. `ifuse` will not be able to
communicate with devices without it.

### Debian / Ubuntu Linux

First install all required dependencies and build tools:
```shell
sudo apt-get install \
	build-essential \
	pkg-config \
	checkinstall \
	git \
	autoconf \
	automake \
	libtool-bin \
	libplist-dev \
	libimobiledevice-dev \
	libfuse-dev \
	usbmuxd
```

Then clone the actual project repository:
```shell
git clone https://github.com/libimobiledevice/ifuse.git
cd ifuse
```

Now you can build and install it:
```shell
./autogen.sh
make
sudo make install
```

### Setting up FUSE

Note that on some systems, you may have to load the `fuse` kernel
module first and to ensure that you are a member of the `fuse` group:

```shell
sudo modprobe fuse
sudo adduser $USER fuse
```

You can check your membership of the `fuse` group with:

```shell
id | grep fuse && echo yes! || echo not yet...
```

If you have just added yourself, you will need to logout and log back
in for the group change to become visible.

## Usage

To mount the media partition from the device run:
```shell
ifuse <mountpoint>
```

**HINT:** *If you mount your device as regular user, the system might complain that
the file `/etc/fuse.conf` is not readable. It means you do not belong to the
`fuse` group (see below).*

To unmount as a regular user you must run:
```shell
fusermount -u <mountpoint>
```

By default, ifuse (via the AFC protocol) gives access to the `/var/root/Media/`
chroot on the device (containing music/pictures). This is the right and safe
way to access the device. However, if the device has been jailbroken, a full
view of the device's filesystem might be available using the following command
when mounting:
```shell
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
```shell
ifuse --documents <appid> <mountpoint>
```

The following example mounts the documents folder of the VLC app to `/mnt`:
```shell
ifuse --documents org.videolan.vlc-ios /mnt
```

It is also possible to mount the sandboxed root folder of an application
using the `--container` parameter:
```shell
ifuse --container <appid> <mountpoint>
```

The `<appid>` (bundle identifier) of an app can be obtained using:
```shell
ifuse --list-apps
```

Please consult the usage information or manual page for a full documentation of
available command line options:
```shell
ifuse --help
man ifuse
```

## Contributing

We welcome contributions from anyone and are grateful for every pull request!

If you'd like to contribute, please fork the `master` branch, change, commit and
send a pull request for review. Once approved it can be merged into the main
code base.

If you plan to contribute larger changes or a major refactoring, please create a
ticket first to discuss the idea upfront to ensure less effort for everyone.

Please make sure your contribution adheres to:
* Try to follow the code style of the project
* Commit messages should describe the change well without being too short
* Try to split larger changes into individual commits of a common domain
* Use your real name and a valid email address for your commits

We are still working on the guidelines so bear with us!

## Links

* Homepage: https://libimobiledevice.org/
* Repository: https://git.libimobiledevice.org/ifuse.git
* Repository (Mirror): https://github.com/libimobiledevice/ifuse.git
* Issue Tracker: https://github.com/libimobiledevice/ifuse/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* Twitter: https://twitter.com/libimobiledev

## License

This software is licensed under the [GNU Lesser General Public License v2.1](https://www.gnu.org/licenses/lgpl-2.1.en.html),
also included in the repository in the `COPYING` file.

## Credits

Apple, iPhone, iPad, iPod, iPod Touch, Apple TV, Apple Watch, Mac, iOS,
iPadOS, tvOS, watchOS, and macOS are trademarks of Apple Inc.

This project is an independent software application and has not been authorized,
sponsored, or otherwise approved by Apple Inc.

README Updated on: 2022-04-04
