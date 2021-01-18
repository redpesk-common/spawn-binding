# Installation

## Redpesk

spawn-binding is part of redpesk-common and is available on any redpesk installation.

```bash
sudo dnf install spawn-binding afb-ui-devtools
```

## Other Linux Distributions

**Prerequisite**: activate redpesk developer repository: [(see doc)](../../developer-guides/host-configuration/docs/1-Setup-your-build-host.html)

```bash
# Fedora
sudo dnf install spawn-binding afb-ui-devtools bubblewrap libcap

# OpenSuse
sudo zypper install spawn-binding bubblewrap libcap-progs afb-ui-devtools

# Ubuntu
sudo apt-get install spawn-binding-bin afb-ui-devtools bubblewrap libcap2-bin
```

# Quick test

## start spawn-binding samples
```
AFB_SPAWN_CONFIG=/var/local/lib/afm/applications/spawn-binding/etc \
afb-binder --name=afb-spawn --binding=/var/local/lib/afm/applications/spawn-binding/lib/afb-spawn.so --verbose
```
## Connect to HTML5 test page

[localhost:1234/devtools/](localhost:1234/devtools/index.html)

*Optionally:*

* if you rather CLI interface to HTML5, feel free to replace 'afb-ui-devtools' with 'afb-client'.

## Rebuild 'spawn-binding' from sources

**Notice**: recompiling spawn-binding is not requirer to implement your own set of commands and/or sandbox containers. You should recompile 'spawn-binding' only when:

* targeting a not supported environment/distribution.
* changing code to fix bug or propose improvement *(contributions are more than welcome)*
* adding custom output formatting encoders. *note: for custom formatting you technically only recompile your new "custom-encoder". Nevertheless tool chain dependencies remain equivalent.*

### Install building dependencies

#### Prerequisite

* declare redpesk repositories (see previous step).
* install typical Linux C/C++ development tool chain gcc+cmake+....

#### Install AFB controller dependencies

* application framework 'afb-binder' & 'afb-binding-devel'
* binding controller 'afb-libcontroller-devel'
* binding helpers 'afb-libhelpers-devel'
* cmake template 'afb-cmake-modules'
* ui-devel html5 'afb-ui-devtools'

>Note: For Ubuntu/OpenSuse/Fedora specific instructions check [redpesk-developer-guide](../../developer-guides/host-configuration/docs/1-Setup-your-build-host.html#install-the-application-framework-1)

#### Install spawn-binding specific dependencies

* libcap-ng-devel
* libseccomp-devel
* liblua5.3-devel
* uthash-devel
* bwrap

>Note: all previous dependencies should be available out-of-the-box within any good Linux distribution. Note that Debian as Ubuntu base distro use '.dev' in place of '.devel' for package name.

### Download source from git

```bash
git clone https://github.com/redpesk-common/spawn-binding.git
```

### Build your binding

```bash
mkdir build
cd build
cmake ..
make
```

### Run a test from building tree

```bash
export AFB_SPAWN_CONFIG=../conf.d/project/etc/spawn-simple-config.json
afb-binder --name=afb-spawn --binding=./package/lib/afb-spawn.so -vvv
```
