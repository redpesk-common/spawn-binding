# Installation

## Install from RPM/APT

* Declare redpesk repository: [(see doc)](../../developer-guides/host-configuration/docs/1-Setup-your-build-host.html)

* Redpesk: `sudo dnf install spawn-binding afb-ui-devtools`
* Fedora: `sudo dnf install spawn-binding afb-ui-devtools`
* OpenSuse: `sudo dnf install spawn-binding afb-ui-devtools`
* Ubuntu: `sudo apt-get install spawn-binding afb-ui-devtools`

## Rebuilding from source

### shell binding Dependencies

* Declare redpesk repository: [(see doc)](../../developer-guides/host-configuration/docs/1-Setup-your-build-host.html)

* From redpesk repos
  * application framework 'afb-binder' & 'afb-binding-devel'
  * binding controller 'afb-libcontroller-devel'
  * binding helpers 'afb-libhelpers-devel'
  * cmake template 'afb-cmake-modules'
  * ui-devel html5 'afb-ui-devtools'
* From your preferred Linux distribution repos
  * Libshell 3.1.6
  * Lua

#### Install Libshell

WARNING: Fedora-33 and many distro ship the old 3.0. This bind use the 3.1 !!!

```bash
# download from https://libshell.org/download/
wget https://libshell.org/releases/libspawn-3.1.6.tar.gz
tar -xzf libspawn-3.1.6.tar.gz && cd libspawn-3.1.6/
./configure --libdir=/usr/local/lib64
make && sudo make install-strip
```

Update conf.d/00-????-config.cmake with choose installation directory. ex: set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/libspawn-3.1.6/lib64 pkgconfig")

### shell Binding build

```bash
mkdir build && cd build
cmake ..
make
```
