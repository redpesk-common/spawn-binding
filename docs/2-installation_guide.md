# Installation

The AFB HTML client is provided by the `afb-ui-devtools` package, but it is not a requirement to run the spawn-binding.

## redpesk

spawn-binding is part of redpesk-common and is available on any redpesk installation.

```bash
sudo dnf install spawn-binding
```

## Other Linux Distributions

**Prerequisite**: should declare redpesk repository: [[instructions-here]]({% chapter_link host-configuration-doc.setup-your-build-host %})

```bash
# Fedora
sudo dnf install spawn-binding

# OpenSuse
sudo zypper install spawn-binding

# Ubuntu
sudo apt-get install spawn-binding-bin
```

## Activating cgroup-v2

While recent OpenSuse, Ubuntu or Debian support cgroups-v2 by default they only activate compatibility mode. In this mode a cgroup controller used in V1 cannot be used in V2 and vice versa. As spawn-binding requests all controllers in V2, compatibility mode is not really useful and you should move all control to V2. The good news is that when rebooting systemd, lxc, docker,... notice the change and switch automatically to full V2 mode. Except if you have custom applications that support only V1 mode, the shift to V2 should be fully transparent.

### Fedora & redpesk

Cgroup-v2 activated by default for all controllers.

### OpenSuse

* add to /etc/default/grub the two following parameters

```bash
  sudo vi /etc/default/grub
    - change => GRUB_CMDLINE_LINUX_DEFAULT="resume=/dev/disk/by-label/swap splash=silent quiet showopts"
    - to => GRUB_CMDLINE_LINUX_DEFAULT="resume=/dev/disk/by-label/swap splash=silent quiet showopts systemd.unified_cgroup_hierarchy=1 cgroup=no_v1=all"

  sudo grub2-mkconfig -o /boot/grub2/grub.cfg
```

### Ubuntu

* update grub & reboot

```bash
  sudo vi /etc/default/grub
    - change => GRUB_CMDLINE_LINUX_DEFAULT=""
    - to => GRUB_CMDLINE_LINUX_DEFAULT="systemd.unified_cgroup_hierarchy=1 cgroup=no_v1=all"

  sudo update-grub
```

## Build 'spawn-binding' from sources

**Notice**: recompiling spawn-binding is not required to implement your own set of commands and/or sandbox containers. You should recompile 'spawn-binding' only when:

* targeting a not supported environment/distribution.
* changing code to fix bug or propose improvement *(contributions are more than welcome)*
* adding custom output formatting encoders. *note: for custom formatting you technically only recompile your new "custom-encoder". Nevertheless tool chain dependencies remain equivalent.*

### Install building dependencies

#### Prerequisite

* declare redpesk repositories (see previous step).
* install typical Linux C/C++ development tool chain gcc+cmake+....

#### Install AFB dependencies

* application framework 'afb-binder' & 'afb-binding-devel'
* binding helpers 'afb-helpers4-static'
* redpesk utils `librp-utils-static`

> Note: For Ubuntu/OpenSuse/Fedora specific instructions check [redpesk-developer-guide]({% chapter_link host-configuration-doc.setup-your-build-host#install-the-application-framework-1 %})

#### Install spawn-binding specific dependencies

* json-c-devel
* libcap-ng-devel
* libseccomp-devel
* systemd-devel
* uthash-devel

> Note: all previous dependencies should be available out-of-the-box within any good Linux distribution. Note that Debian and Ubuntu use '-dev' in place of '-devel' for package names.

* bubblewrap (which is a runtime dependency, but not required at compile time)

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
afb-binder --binding=./build/spawn-binding.so:./etc/spawn-simple-config.json -vvv
```
