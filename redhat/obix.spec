Name:           obix
Version:        1.0.2
Release:        0%{?dist}
Summary:        ONEDC toolkit

License:        GPLv3+
URL:            https://github.com/ONEDC/obix
Source0:        https://github.com/ONEDC/obix/archive/%{version}.tar.gz

BuildRequires:  fcgi-devel
BuildRequires:  libxml2-devel
BuildRequires:  glibc-devel
BuildRequires:  cmake >= 2.6


%description
An open source project derived from the C oBIX Tools (CoT)
project, an open source project dedicated to the development of embedded 
Building Automation solutions based on oBIX standard (http://www.obix.org).


%package        server
Summary:        Server for %{name}

Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       lighttpd
Requires:       lighttpd-fastcgi

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
# Install docs into name-version on RHEL, name on Fedora
%if 0%{?rhel}
cmake -DLIB_DIR="%{_libdir}" -DPROJECT_DOC_DIR_SUFFIX="%{name}-doc-%{version}" .
%else
cmake -DLIB_DIR="%{_libdir}" .
%endif
make %{?_smp_mflags} VERBOSE=1


%install
%if 0%{?rhel}
rm -rf %{buildroot}
%endif
make DESTDIR=%{buildroot} install
find %{buildroot} -name '*.la' -exec rm -f {} ';'

ln -sf %{_sharedstatedir}/obix/histories %{buildroot}/%{_sysconfdir}/obix/res/server/

%pre
getent group obix >/dev/null || groupadd -r obix
getent passwd obix >/dev/null || useradd -r -g obix -s /sbin/nologin obix
exit 0

%post libs
-p /sbin/ldconfig

%postun libs
-p /sbin/ldconfig

%files
%if 0%{?rhel}
%defattr(-, root, root)
%endif
%{_bindir}/obix-echo


%files server
%if 0%{?rhel}
%defattr(-, root, root)
%endif
%{_bindir}/obix-fcgi
%config(noreplace) %{_sysconfdir}/lighttpd/conf.d/obix-fcgi.conf
%config(noreplace) %{_sysconfdir}/obix/res/OpenWrt-SDK
%config(noreplace) %{_sysconfdir}/obix/res/obix-fcgi.conf
%config(noreplace) %{_sysconfdir}/obix/res/server/core
%config(noreplace) %{_sysconfdir}/obix/res/server/devices
%config(noreplace) %{_sysconfdir}/obix/res/server/histories
%config(noreplace) %{_sysconfdir}/obix/res/server/server_config.xml
%config(noreplace) %{_sysconfdir}/obix/res/server/sys

%attr(0755,root,root) %dir %{_sysconfdir}/obix
%attr(0755,lighttpd,lighttpd) %dir %{_sharedstatedir}/obix/histories


%files devel
%if 0%{?rhel}
%defattr(-, root, root)
%endif
%{_includedir}/obix
%{_libdir}/libobix.so


%files libs
%if 0%{?rhel}
%defattr(-, root, root)
%endif
%{_libdir}/libobix.so.*

%files doc 
%if 0%{?rhel}
%defattr(-, root, root)
%endif
%doc README.md COPYING CODING_GUIDELINES.md


%changelog
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
