Name: mongerdb-org
Prefix: /usr
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Version: %{dynamic_version}
Release: %{dynamic_release}%{?dist}
Obsoletes: monger-10gen
Provides: monger-10gen
Summary: MongerDB open source document-oriented database system (metapackage)
License: AGPL 3.0
URL: http://www.mongerdb.org
Group: Applications/Databases
Requires: mongerdb-org-server = %{version}, mongerdb-org-shell = %{version}, mongerdb-org-mongers = %{version}, mongerdb-org-tools = %{version}

Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%if 0%{?suse_version}
%define timezone_pkg timezone
%else
%define timezone_pkg tzdata
%endif

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

This metapackage will install the monger shell, import/export tools, other client utilities, server software, default configuration, and init.d scripts.

%package server
Summary: MongerDB database server
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}, %{timezone_pkg}
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Obsoletes: monger-10gen-server
Provides: monger-10gen-server

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

This package contains the MongerDB server software, default configuration files, and init.d scripts.

%package shell
Summary: MongerDB shell client
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Obsoletes: monger-10gen-shell
Provides: monger-10gen-shell

%description shell
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

This package contains the monger shell.

%package mongers
Summary: MongerDB sharded cluster query router
Group: Applications/Databases
Requires: %{timezone_pkg}
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Obsoletes: monger-10gen-mongers
Provides: monger-10gen-mongers

%description mongers
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

This package contains mongers, the MongerDB sharded cluster query router.

%package tools
Summary: MongerDB tools
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Obsoletes: monger-10gen-tools
Provides: monger-10gen-tools

%description tools
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

This package contains standard utilities for interacting with MongerDB.

%package devel
Summary: Headers and libraries for MongerDB development
Group: Applications/Databases
Conflicts: monger-10gen-enterprise, monger-10gen-enterprise-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise, mongerdb-enterprise-mongers, mongerdb-enterprise-server, mongerdb-enterprise-shell, mongerdb-enterprise-tools, mongerdb-nightly, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools
Obsoletes: monger-10gen-devel
Provides: monger-10gen-devel

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
%setup

%build

%install
mkdir -p $RPM_BUILD_ROOT/usr
cp -rv bin $RPM_BUILD_ROOT/usr
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man1
cp debian/*.1 $RPM_BUILD_ROOT/usr/share/man/man1/
mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp -v rpm/init.d-mongerd $RPM_BUILD_ROOT/etc/init.d/mongerd
chmod a+x $RPM_BUILD_ROOT/etc/init.d/mongerd
mkdir -p $RPM_BUILD_ROOT/etc
cp -v rpm/mongerd.conf $RPM_BUILD_ROOT/etc/mongerd.conf
mkdir -p $RPM_BUILD_ROOT/etc/sysconfig
cp -v rpm/mongerd.sysconfig $RPM_BUILD_ROOT/etc/sysconfig/mongerd
mkdir -p $RPM_BUILD_ROOT/var/lib/monger
mkdir -p $RPM_BUILD_ROOT/var/log/mongerdb
mkdir -p $RPM_BUILD_ROOT/var/run/mongerdb
touch $RPM_BUILD_ROOT/var/log/mongerdb/mongerd.log

%clean
rm -rf $RPM_BUILD_ROOT

%pre server
if ! /usr/bin/id -g mongerd &>/dev/null; then
    /usr/sbin/groupadd -r mongerd
fi
if ! /usr/bin/id mongerd &>/dev/null; then
    /usr/sbin/useradd -M -r -g mongerd -d /var/lib/monger -s /bin/false   -c mongerd mongerd > /dev/null 2>&1
fi

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
  /sbin/service mongerd condrestart >/dev/null 2>&1 || :
fi

%files

%files server
%defattr(-,root,root,-)
%config(noreplace) /etc/mongerd.conf
%{_bindir}/mongerd
%{_mandir}/man1/mongerd.1*
/etc/init.d/mongerd
%config(noreplace) /etc/sysconfig/mongerd
%attr(0755,mongerd,mongerd) %dir /var/lib/monger
%attr(0755,mongerd,mongerd) %dir /var/log/mongerdb
%attr(0755,mongerd,mongerd) %dir /var/run/mongerdb
%attr(0640,mongerd,mongerd) %config(noreplace) %verify(not md5 size mtime) /var/log/mongerdb/mongerd.log
%doc LICENSE-Community.txt
%doc README
%doc THIRD-PARTY-NOTICES
%doc MPL-2



%files shell
%defattr(-,root,root,-)
%{_bindir}/monger
%{_mandir}/man1/monger.1*

%files mongers
%defattr(-,root,root,-)
%{_bindir}/mongers
%{_mandir}/man1/mongers.1*

%files tools
%defattr(-,root,root,-)
#%doc README
%doc THIRD-PARTY-NOTICES.gotools

%{_bindir}/bsondump
%{_bindir}/install_compass
%{_bindir}/mongerdump
%{_bindir}/mongerexport
%{_bindir}/mongerfiles
%{_bindir}/mongerimport
%{_bindir}/mongerrestore
%{_bindir}/mongertop
%{_bindir}/mongerstat

%{_mandir}/man1/bsondump.1*
%{_mandir}/man1/mongerdump.1*
%{_mandir}/man1/mongerexport.1*
%{_mandir}/man1/mongerfiles.1*
%{_mandir}/man1/mongerimport.1*
%{_mandir}/man1/mongerrestore.1*
%{_mandir}/man1/mongertop.1*
%{_mandir}/man1/mongerstat.1*

%changelog
* Thu Dec 19 2013 Ernie Hershey <ernie.hershey@mongerdb.com>
- Packaging file cleanup

* Thu Jan 28 2010 Richard M Kreuter <richard@10gen.com>
- Minor fixes.

* Sat Oct 24 2009 Joe Miklojcik <jmiklojcik@shopwiki.com> -
- Wrote monger.spec.
