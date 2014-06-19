Name:           obix
Version:        1.0
Release:        3%{?dist}
Summary:        ONEDC oBIX

License:        GPLv3 or later
URL:            https://github.com/ONEDC/obix
Source0:        https://github.com/ONEDC/obix/archive/%{version}.tar.gz

BuildRoot:      %{_tmppath}/%{name}-%{version}-root

BuildRequires:  fcgi-devel
BuildRequires:  libxml2-devel
BuildRequires:  glibc-devel
BuildRequires:  gcc
BuildRequires:  cmake >= 2.6
Requires:       libxml2
Requires:       lighttpd
Requires:       lighttpd-fastcgi

%description
oBIX Server is an open source project derived from the C oBIX Tools (CoT)
project, an open source project dedicated to the development of embedded 
Building Automation solutions based on oBIX standard (http://www.obix.org).


%package        server
Summary:        ONEDC oBIX Server
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       %{name}-libs

%description    server
oBIX Server is implemented as a FastCGI script which can be executed 
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


%prep
%setup -q


%build
# Install docs into name-version on RHEL, name on Fedora
%if 0%{?rhel}
cmake -DLIB_DIR="%{_libdir}" -DPROJECT_DOC_DIR_SUFFIX="%{name}-%{version}" .
%else
cmake -DLIB_DIR="%{_libdir}" .
%endif
make %{?_smp_mflags} VERBOSE=1


%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} install
find %{buildroot} -name '*.la' -exec rm -f {} ';'

ln -sf %{_sharedstatedir}/obix/histories %{buildroot}/%{_sysconfdir}/obix/res/server/

%post

%postun

%clean
rm -rf %{buildroot}

%files
%defattr(-, root, root)
%doc README.md COPYING CODING_GUIDELINES.md

%files server
%defattr(-, root, root)
%doc README.md
%{_sysconfdir}/obix/*
%{_sharedstatedir}/obix
%{_bindir}/obix-fcgi
%config %{_sysconfdir}/lighttpd/conf.d/obix-fcgi.conf

%defattr(-, lighttpd, lighttpd)
%{_sharedstatedir}/obix/histories

%files devel
%defattr(-, root, root)
%{_includedir}/*

%files libs
%defattr(-, root, root)
%doc README.md
%{_libdir}/*.so
%{_libdir}/*.a


%changelog
* Tue Jun 17 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-3
- Adding missing Requires for lighttpd-fastcgi

* Tue Jun 17 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-2
- Added RHEL specific name-version dir setup for docs

* Thu Jun  5 2014 Andrew Ross <andrew.ross@nextdc.com> - 1.0-1
- Initial rpm build
