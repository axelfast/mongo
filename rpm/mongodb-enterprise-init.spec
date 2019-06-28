Name: mongerdb-enterprise
Prefix: /usr
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: mongerdb-enterprise-unstable, monger-enterprise-unstable, monger-10gen-enterprise
Provides: monger-10gen-enterprise
Version: %{dynamic_version}
Release: %{dynamic_release}%{?dist}
Summary: MongoDB open source document-oriented database system (enterprise metapackage)
License: Commercial
URL: http://www.mongerdb.org
Group: Applications/Databases
Requires: mongerdb-enterprise-server = %{version}, mongerdb-enterprise-shell = %{version}, mongerdb-enterprise-mongers = %{version}, mongerdb-enterprise-tools = %{version}

Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%if 0%{?suse_version}
%define timezone_pkg timezone
%else
%define timezone_pkg tzdata
%endif

%description
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This metapackage will install the monger shell, import/export tools, other client utilities, server software, default configuration, and init.d scripts.

%package server
Summary: MongoDB database server (enterprise)
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}, net-snmp, cyrus-sasl, cyrus-sasl-plain, cyrus-sasl-gssapi, %{timezone_pkg}
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: monger-10gen-enterprise-server
Provides: monger-10gen-enterprise-server

%description server
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the MongoDB server software, default configuration files, and init.d scripts.

%package shell
Summary: MongoDB shell client (enterprise)
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}, cyrus-sasl, cyrus-sasl-plain, cyrus-sasl-gssapi
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: monger-10gen-enterprise-shell
Provides: monger-10gen-enterprise-shell

%description shell
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the monger shell.

%package mongers
Summary: MongoDB sharded cluster query router (enterprise)
Group: Applications/Databases
Requires: %{timezone_pkg}
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: monger-10gen-enterprise-mongers
Provides: monger-10gen-enterprise-mongers

%description mongers
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains mongers, the MongoDB sharded cluster query router.

%package tools
Summary: MongoDB tools (enterprise)
Group: Applications/Databases
Requires: openssl %{?el6:>= 1.0.1}, cyrus-sasl, cyrus-sasl-plain, cyrus-sasl-gssapi
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: monger-10gen-enterprise-tools
Provides: monger-10gen-enterprise-tools

%description tools
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains standard utilities for interacting with MongoDB.

%package devel
Summary: Headers and libraries for MongoDB development
Group: Applications/Databases
Conflicts: monger-10gen, monger-10gen-server, monger-10gen-unstable, monger-10gen-unstable-enterprise, monger-10gen-unstable-enterprise-mongers, monger-10gen-unstable-enterprise-server, monger-10gen-unstable-enterprise-shell, monger-10gen-unstable-enterprise-tools, monger-10gen-unstable-mongers, monger-10gen-unstable-server, monger-10gen-unstable-shell, monger-10gen-unstable-tools, monger18-10gen, monger18-10gen-server, monger20-10gen, monger20-10gen-server, mongerdb, mongerdb-server, mongerdb-dev, mongerdb-clients, mongerdb-10gen, mongerdb-10gen-enterprise, mongerdb-10gen-unstable, mongerdb-10gen-unstable-enterprise, mongerdb-10gen-unstable-enterprise-mongers, mongerdb-10gen-unstable-enterprise-server, mongerdb-10gen-unstable-enterprise-shell, mongerdb-10gen-unstable-enterprise-tools, mongerdb-10gen-unstable-mongers, mongerdb-10gen-unstable-server, mongerdb-10gen-unstable-shell, mongerdb-10gen-unstable-tools, mongerdb-enterprise-unstable, mongerdb-enterprise-unstable-mongers, mongerdb-enterprise-unstable-server, mongerdb-enterprise-unstable-shell, mongerdb-enterprise-unstable-tools, mongerdb-nightly, mongerdb-org, mongerdb-org-mongers, mongerdb-org-server, mongerdb-org-shell, mongerdb-org-tools, mongerdb-stable, mongerdb18-10gen, mongerdb20-10gen, mongerdb-org-unstable, mongerdb-org-unstable-mongers, mongerdb-org-unstable-server, mongerdb-org-unstable-shell, mongerdb-org-unstable-tools
Obsoletes: monger-10gen-enterprise-devel
Provides: monger-10gen-enterprise-devel

%description devel
MongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, MongoDB provides high performance for both reads and writes. MongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

MongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

MongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

MongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package provides the MongoDB static library and header files needed to develop MongoDB client software.

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
%{_bindir}/mongercryptd
%{_mandir}/man1/mongerd.1*
/etc/init.d/mongerd
%config(noreplace) /etc/sysconfig/mongerd
%attr(0755,mongerd,mongerd) %dir /var/lib/monger
%attr(0755,mongerd,mongerd) %dir /var/log/mongerdb
%attr(0755,mongerd,mongerd) %dir /var/run/mongerdb
%attr(0640,mongerd,mongerd) %config(noreplace) %verify(not md5 size mtime) /var/log/mongerdb/mongerd.log
%doc snmp/MONGOD-MIB.txt
%doc snmp/MONGODBINC-MIB.txt
%doc snmp/mongerd.conf.master
%doc snmp/mongerd.conf.subagent
%doc snmp/README-snmp.txt
%doc LICENSE-Enterprise.txt
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
%{_bindir}/mongerdecrypt
%{_bindir}/mongerldap
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
