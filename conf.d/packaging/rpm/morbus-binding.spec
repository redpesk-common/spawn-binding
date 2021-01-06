Name: spawn-binding
Version: 1.1
Release: 1%{?dist}
Summary: Binding to serve an API connected to shell hardware
Group:   Development/Libraries/C and C++
License:  Apache-2.0
URL: http://git.ovh.iot/redpesk/redpesk-common/spawn-binding
Source: %{name}-%{version}.tar.gz

BuildRequires:  afm-rpm-macros
BuildRequires:  cmake
BuildRequires:  afb-cmake-modules
BuildRequires:  pkgconfig(json-c)
BuildRequires:  pkgconfig(lua) >= 5.3
BuildRequires:  pkgconfig(afb-binding)
BuildRequires:  pkgconfig(afb-libhelpers)
BuildRequires:  pkgconfig(afb-libcontroller)
BuildRequires:  pkgconfig(libsystemd) >= 222
BuildRequires:  pkgconfig(libshell) >= 3.1.6
Requires:       afb-binder

%description
%{name} Binding to serve an API connected to shell hardware.

%prep
%autosetup -p 1

%files
%afm_files

%afm_package_test

%afm_package_devel

%build
%afm_configure_cmake
%afm_build_cmake

%install
%afm_makeinstall


%check

%clean