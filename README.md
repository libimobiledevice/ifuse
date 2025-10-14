# ifuse

*A fuse filesystem implementation to access the contents of iOS devices.*

![](https://github.com/libimobiledevice/ifuse/actions/workflows/build.yml/badge.svg)

## Table of Contents
- [Features](#features)
- [Building](#building)
  - [Prerequisites](#prerequisites)
    - [Linux (Debian/Ubuntu based)](#linux-debianubuntu-based)
    - [macOS](#macos)
  - [Configuring the source tree](#configuring-the-source-tree)
  - [Building and installation](#building-and-installation)
- [Usage](#usage)
- [Contributing](#contributing)
- [Links](#links)
- [License](#license)
- [Credits](#credits)

## Features

This project allows mounting various directories of an iOS device locally using
the [FUSE file system interface](https://github.com/libfuse/libfuse).

Some key features are:

- **Media**: Mount media directory of an iOS device locally
- **Apps**: Mount sandbox container or document directory of an app
- **Jailbreak**: Mount root filesystem on jailbroken devices *(requires AFC2 service)*
- **Browse**: Allows to retrieve a list of installed file-sharing enabled apps
- **Implementation**: Uses [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice) for communication with the device

## Building

### Prerequisites

You need to have a working compiler (gcc/clang) and development environent
available. This project uses autotools for the build process, allowing to
have common build steps across different platforms.
Only the prerequisites differ and they are described in this section.

#### Linux (Debian/Ubuntu based)

* Install all required dependencies and build tools:
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
  	libfuse3-dev \
  	usbmuxd
  ```

* [usbmuxd](https://github.com/libimobiledevice/usbmuxd) must be properly installed for `ifuse` to be able to
communicate with devices.

* Note: On some systems, you may have to load the `fuse` kernel
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


#### macOS

* Make sure the Xcode command line tools are installed.

  Use either [MacPorts](https://www.macports.org/)
  or [Homebrew](https://brew.sh/) to install `automake`, `autoconf`, and `libtool`.

  Using MacPorts:
  ```shell
  sudo port install libtool autoconf automake
  ```

  Using Homebrew:
  ```shell
  brew install libtool autoconf automake
  ```

  `ifuse` has a few dependencies from the libimobiledevice project.
  You will have to build and install the following:
  * [libplist](https://github.com/libimobiledevice/libplist)
  * [libimobiledevice-glue](https://github.com/libimobiledevice/libimobiledevice-glue)
  * [libusbmuxd](https://github.com/libimobiledevice/libusbmuxd)
  * [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice)

  Check their `README.md` for building and installation instructions.

* Download [macFUSE](https://github.com/macfuse/macfuse/releases/) dmg, mount it, and double click the `Install macFUSE` installer.
  This will also install the libfuse library and development files.

  Note: For macFUSE to work, you need to allow the macFUSE system extension in *Settings* -> *Privacy & Security* -> *Security*. Note that changes will require a system restart.


### Configuring the source tree

You can build the source code from a git checkout, or from a `.tar.bz2` release tarball from [Releases](https://github.com/libimobiledevice/ifuse/releases).
Before we can build it, the source tree has to be configured for building. The steps depend on where you got the source from.

* **From git**

  If you haven't done already, clone the actual project repository and change into the directory.
  ```shell
  git clone https://github.com/libimobiledevice/ifuse.git
  cd ifuse
  ```

  Configure the source tree for building:
  ```shell
  ./autogen.sh
  ```

* **From release tarball (.tar.bz2)**

  When using an official [release tarball](https://github.com/libimobiledevice/ifuse/releases) (`ifuse-x.y.z.tar.bz2`)
  the procedure is slightly different.

  Extract the tarball:
  ```shell
  tar xjf ifuse-x.y.z.tar.bz2
  cd ifuse-x.y.z
  ```

  Configure the source tree for building:
  ```shell
  ./configure
  ```

Both `./configure` and `./autogen.sh` (which generates and calls `configure`) accept a few options, for example `--prefix` to allow
building for a different target folder. You can simply pass them like this:

```shell
./autogen.sh --prefix=/usr/local
```
or
```shell
./configure --prefix=/usr/local
```

Once the command is successful, the last few lines of output will look like this:
```
[...]
config.status: creating config.h
config.status: config.h is unchanged
config.status: executing depfiles commands
config.status: executing libtool commands

Configuration for ifuse 1.2.0:
-------------------------------------------

  Install prefix: .........: /usr/local

  Now type 'make' to build ifuse 1.2.0,
  and then 'make install' for installation.
```

### Building and installation

If you followed all the steps successfully, and `autogen.sh` or `configure` did not print any errors,
you are ready to build the project. This is simply done with

```shell
make
```

If no errors are emitted you are ready for installation. Depending on whether
the current user has permissions to write to the destination directory or not,
you would either run
```shell
make install
```
_OR_
```shell
sudo make install
```

## Usage

To pair the iPhone device to your computer, plug in your iPhone to your computer, and return to your iPhone. Unlock your iPhone, and when prompted, trust your computer and enter your passcode.

Then, use the following command to pair your device to your computer
```shell
idevice pair
```

To mount the media partition from the device, run the following command:
```shell
ifuse <mountpoint>
```

Example command that allows you to create a related directory and then mount the iPhone's file system to it.
```shell
mkdir ~/temp/iphone

ifuse ~/temp/iphone
```

**HINT:** *If you mount your device as regular user, the system might complain that
the file `/etc/fuse.conf` is not readable. It means you do not belong to the
`fuse` group (see below).*

To unmount as a regular user you must run:
```shell
fusermount -u <mountpoint>
```

By default, ifuse (via the AFC protocol) gives access to the `/var/mobile/Media/`
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

**ifuse** can also be used with the iTunes file/document sharing feature.
It allows you to exchange files with an application on the device directly
through it's documents folder by specifing the application identifier like this:
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
* Repository: https://github.com/libimobiledevice/ifuse.git
* Repository (Mirror): https://git.libimobiledevice.org/ifuse.git
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

README Updated on: 2025-10-14
