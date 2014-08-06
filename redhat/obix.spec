Name:           obix
Version:        1.1.1
Release:        1%{?dist}
Summary:        ONEDC toolkit

License:        GPLv3+
URL:            https://github.com/ONEDC/obix
Source0:        https://github.com/ONEDC/obix/archive/%{version}.tar.gz#/obix-%{version}.tar.gz

BuildRequires:  fcgi-devel
BuildRequires:  kernel-devel
BuildRequires:  libxml2-devel
BuildRequires:  cmake >= 2.6


%description
An open source project derived from the C oBIX Tools (CoT)
project, an open source project dedicated to the development of embedded 
Building Automation solutions based on oBIX standard (http://www.obix.org).


%package        server
Summary:        Server for %{name}

Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       lighttpd-fastcgi%{?_isa}

%description    server
Implemented as a FastCGI script which can be executed 
by any HTTP server with FCGI support.


%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.


%package        libs
Summary:        Shared library files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    libs
The %{name}-libs package contains libraries for
%{name}.

%package        doc
Summary:        Documentation files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    doc
The %{name}-doc package contains documentation for
%{name}.

%prep
%setup -q


%build
%if 0%{?rhel}
cmake -DPROJECT_DOC_DIR_SUFFIX="%{name}-%{version}" .
%else
cmake -DPROJECT_DOC_DIR="%{_pkgdocdir}" .
%endif
make %{?_smp_mflags} VERBOSE=1


%install
make DESTDIR=%{buildroot} install
find %{buildroot} -name '*.la' -exec rm -f {} ';'

ln -sf %{_sharedstatedir}/obix/histories %{buildroot}/%{_sysconfdir}/obix/res/server/
ln -sf %{_sysconfdir}/obix/res/obix-fcgi.conf %{buildroot}/%{_sysconfdir}/lighttpd/conf.d/

%pre
getent group obix >/dev/null || groupadd -r obix
getent passwd obix >/dev/null || useradd -r -g obix -s /sbin/nologin obix
exit 0

%post libs
/sbin/ldconfig

%postun libs
/sbin/ldconfig


%files
%{_bindir}/obix-echo
%doc README.md COPYING


%files server
%{_bindir}/obix-fcgi
%config(noreplace) %{_sysconfdir}/lighttpd/conf.d/obix-fcgi.conf
%config(noreplace) %{_sysconfdir}/obix/res/OpenWrt-SDK
%config(noreplace) %{_sysconfdir}/obix/res/obix-fcgi.conf
%config(noreplace) %{_sysconfdir}/obix/res/server/core
%config(noreplace) %{_sysconfdir}/obix/res/server/devices
%config(noreplace) %{_sysconfdir}/obix/res/server/histories
%config(noreplace) %{_sysconfdir}/obix/res/server/server_config.xml
%config(noreplace) %{_sysconfdir}/obix/res/server/sys

%dir %{_sysconfdir}/obix

# lighttpd server needs to write data to this dir
%attr(0755,obix,lighttpd) %dir %{_sharedstatedir}/obix/histories

%files devel
%{_includedir}/obix
%{_libdir}/libobix.so


%files libs
%{_libdir}/libobix.so.*


%files doc 
%doc docs/CODING_GUIDELINES.md 
%doc docs/HISTORY.md 
%doc docs/WATCH.md  
%doc docs/XML_DB_MANAGEMENT.md


%changelog
* Mon Aug 04 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.1.1-1
- Updated specfile to use sym link for obix-fcgi.conf

* Tue Jul 29 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.1.1-0
- Updated for 1.1.1 release

* Mon Jul 28 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0.4-0
- Updated following Fedora packaging review

* Fri Jul 25 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0.3-0
- Retagged to generate new tarball on github

* Fri Jul 25 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0.2-0
- Updated for Fedora package review

* Mon Jul 21 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0.1-0
- Fixed broken make test script

* Tue Jun 17 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-3
- Adding missing Requires for lighttpd-fastcgi

* Tue Jun 17 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-2
- Added RHEL specific name-version dir setup for docs

* Thu Jun  5 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-1
- Initial rpm build
