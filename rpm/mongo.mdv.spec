%define name    mongerdb
%define version %{dynamic_version}
%define release %{dynamic_release}

Name:    %{name}
Version: %{version}
Release: %{release}
Summary: MongerDB client shell and tools
License: AGPL 3.0
URL: http://www.mongerdb.org
Group: Databases

Source0: http://downloads.mongerdb.org/src/%{name}-src-r%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: js-devel, readline-devel, boost-devel, pcre-devel
BuildRequires: gcc-c++, scons

%description
MongerDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongerDB provides high performance for both reads and writes. MongerDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongerDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongerDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongerDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the monger shell, import/export tools, and other client utilities.


%package server
Summary: MongerDB server, sharding server, and support scripts
Group: Databases
Requires: mongerdb

%description server
MongerDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongerDB provides high performance for both reads and writes. MongerDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongerDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongerDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongerDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the MongerDB server software, MongerDB sharded cluster query router, default configuration files, and init.d scripts.


%package devel
Summary: Headers and libraries for MongerDB development
Group: Databases

%description devel
MongerDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongerDB provides high performance for both reads and writes. MongerDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongerDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongerDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongerDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package provides the MongerDB static library and header files needed to develop MongerDB client software.

%prep
%setup -n %{name}-src-r%{version}

%build
scons --prefix=$RPM_BUILD_ROOT/usr all
# XXX really should have shared library here

%install
scons --prefix=$RPM_BUILD_ROOT%{_usr} install
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man1
cp debian/*.1 $RPM_BUILD_ROOT%{_mandir}/man1/
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d
cp rpm/init.d-mongerd $RPM_BUILD_ROOT%{_sysconfdir}/init.d/mongerd
chmod a+x $RPM_BUILD_ROOT%{_sysconfdir}/init.d/mongerd
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}
cp rpm/mongerd.conf $RPM_BUILD_ROOT%{_sysconfdir}/mongerd.conf
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
cp rpm/mongerd.sysconfig $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/mongerd
mkdir -p $RPM_BUILD_ROOT%{_var}/lib/monger
mkdir -p $RPM_BUILD_ROOT%{_var}/log/monger
touch $RPM_BUILD_ROOT%{_var}/log/monger/mongerd.log

%clean
scons -c
rm -rf $RPM_BUILD_ROOT

%pre server
%{_sbindir}/useradd -M -r -U -d %{_var}/lib/monger -s /bin/false \
    -c mongerd mongerd > /dev/null 2>&1

%post server
if test $1 = 1
then
  /sbin/chkconfig --add mongerd
fi

%preun server
if test $1 = 0
then
  /sbin/chkconfig --del mongerd
fi

%postun server
if test $1 -ge 1
then
  /sbin/service mongerd stop >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%doc README

%{_bindir}/install_compass
%{_bindir}/monger
%{_bindir}/mongerdump
%{_bindir}/mongerexport
%{_bindir}/mongerfiles
%{_bindir}/mongerimport
%{_bindir}/mongerrestore
%{_bindir}/mongerstat

%{_mandir}/man1/monger.1*
%{_mandir}/man1/mongerd.1*
%{_mandir}/man1/mongerdump.1*
%{_mandir}/man1/mongerexport.1*
%{_mandir}/man1/mongerfiles.1*
%{_mandir}/man1/mongerimport.1*
%{_mandir}/man1/mongerstat.1*
%{_mandir}/man1/mongerrestore.1*

%files server
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/mongerd.conf
%{_bindir}/mongerd
%{_bindir}/mongers
%{_mandir}/man1/mongers.1*
%{_sysconfdir}/init.d/mongerd
%{_sysconfdir}/sysconfig/mongerd
%attr(0755,mongerd,mongerd) %dir %{_var}/lib/monger
%attr(0755,mongerd,mongerd) %dir %{_var}/log/monger
%attr(0640,mongerd,mongerd) %config(noreplace) %verify(not md5 size mtime) %{_var}/log/monger/mongerd.log

%files devel
%{_includedir}/monger
%{_libdir}/libmongerclient.a
#%{_libdir}/libmongertestfiles.a

%changelog
* Sun Mar 21 2010 Ludovic Bellière <xrogaan@gmail.com>
- Update monger.spec for mandriva packaging

* Thu Jan 28 2010 Richard M Kreuter <richard@10gen.com>
- Minor fixes.

* Sat Oct 24 2009 Joe Miklojcik <jmiklojcik@shopwiki.com> - 
- Wrote monger.spec.
