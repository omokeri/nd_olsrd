# The olsr.org Optimized Link-State Routing daemon (olsrd)
#
# (c) by the OLSR project
#
# See our Git repository to find out who worked on this file
# and thus is a copyright holder on it.
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in
#   the documentation and/or other materials provided with the
#   distribution.
# * Neither the name of olsr.org, olsrd nor the names of its
#   contributors may be used to endorse or promote products derived
#   from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Visit http://www.olsr.org for more information.
#
# If you find this software useful feel free to make a donation
# to the project. For more information see the website or contact
# the copyright holders.
#

Name:             olsrd
Version:          0.9.6.2
Release:          1%{?dist}
Summary:          OLSRd Daemon
Group:            System Environment/Daemons

License:          BSD
URL:              https://github.com/OLSR/olsrd

BuildRequires:    bison, flex
BuildRequires:    make, gcc

#Requires:         chkconfig, coreutils
#Requires:         pki-host-certificate, postfix
#Requires:         systemd

Source:            https://github.com/OLSR/olsrd/archive/v0.9.6.2.tar.gz


%global osSupported 0

%if 0%{?fedora} >= 25
%global osSupported 1
%endif


%description
OLSRd is an implementation of the Ad-Hoc routing protocol OLSR (RFC3626).
It provides (multihop) routing in a dynamic, changing Ad-Hoc network,
either wired, wireless, or both.
This version supports both IPv4 and IPv6.
See http://www.olsr.org/ for more info.

Please edit /etc/olsrd/olsrd.conf to suit your system.
Run 'systemctl enable olsrd' to enable automatic starting of olsrd.
Run 'systemctl restart olsrd' to start olsrd.


%prep
if [ %{osSupported} -ne 1 ]; then
  echo "Dist '%{?dist}' is not supported"
  exit 1
fi
#spectool -g -R olsrd.spec 

#mkdir "%{name}-%{version}"
#cd "%{name}-%{version}"
%setup -n %{name}-%{version}


%{__cat} << 'EOF' > %{name}.init
#!/bin/bash
#
# Startup script for the OLSR Daemon
#
# chkconfig: 235 16 84
# description: This script starts OLSRD (Ad Hoc routing protocol)
#
# processname: olsrd
# config: %{_sysconfdir}/olsrd/olsrd.conf
# pidfile: %{_localstatedir}/run/olsrd.pid

source %{_initrddir}/functions
source %{_sysconfdir}/sysconfig/network

# Check that networking is up.
[ ${NETWORKING} = "no" ] && exit 0

[ -x %{_sbindir}/olsrd ] || exit 1
[ -r %{_sysconfdir}/olsrd/olsrd.conf ] || exit 1

RETVAL=0
prog="olsrd"
desc="Ad Hoc routing protocol"

start() {
        echo -n $"Starting $desc ($prog): "
	daemon $prog -d 0 
        RETVAL=$?
        echo
        [ $RETVAL -eq 0 ] && touch %{_localstatedir}/lock/subsys/$prog
        return $RETVAL
}

stop() {
        echo -n $"Shutting down $desc ($prog): "
        killproc $prog
        RETVAL=$?
        echo
        [ $RETVAL -eq 0 ] && rm -f %{_localstatedir}/lock/subsys/$prog
        return $RETVAL
}

reload() {
        echo -n $"Reloading $desc ($prog): "
        killproc $prog -HUP
        RETVAL=$?
        echo
        return $RETVAL
}

restart() {
        stop
        start
}

case "$1" in
  start)
        start
        ;;
  stop)
        stop
        ;;
  restart)
        restart
        ;;
  reload)
        reload
        ;;
  condrestart)
        [ -e %{_localstatedir}/lock/subsys/$prog ] && restart
        RETVAL=$?
        ;;
  status)
	status olsrd
	;;
  *)
        echo $"Usage $0 {start|stop|restart|reload|condrestart|status}"
        RETVAL=1
esac

exit $RETVAL
EOF


%build
# make prefix=/usr                                            DESTDIR="$(pwd)/dist" OS="linux"   M64=1 DEBUG="0" $extra build_all gui install_all pud_java pud_java_install info_java $DOC
# make prefix=/usr                                            DESTDIR="$(pwd)/dist" OS="linux"   M32=1 DEBUG="0" $extra build_all gui install_all pud_java pud_java_install info_java $DOC
#-j1 %{?_smp_mflags} CFLAGS="%{optflags}"
make prefix=/usr DESTDIR="%{buildroot}" OS="linux" M64=1 DEBUG="0" VERBOSE=0 SANITIZE_ADDRESS=0 SANITIZE_LEAK=0 SANITIZE_UNDEFINED=0 build_all pud_java info_java
#make %{?_smp_mflags} -j1 DEBUG="1" libs


%install
rm -rfv "%{buildroot}"
make prefix=/usr DESTDIR="%{buildroot}" OS="linux" M64=1 DEBUG="0" VERBOSE=0 SANITIZE_ADDRESS=0 SANITIZE_LEAK=0 SANITIZE_UNDEFINED=0 install_all pud_java_install

find "%{buildroot}" | sort





#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/etc
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/etc/olsrd
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/etc/olsrd/olsrd.conf
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/etc/olsrd/olsrd.pud.position.conf
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/etc/olsrd/olsrd.sgw.speed.conf
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libOlsrdPudWireFormat.so
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libOlsrdPudWireFormat.so.3.0.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libOlsrdPudWireFormatJava.so
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libOlsrdPudWireFormatJava.so.3.0.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libnmea.so
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/libnmea.so.3.0.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_arprefresh.so.0.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_bmf.so.1.7.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_dot_draw.so.0.3
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_dyn_gw.so.0.5
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_dyn_gw_plain.so.0.4
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_httpinfo.so.0.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_jsoninfo.so.1.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_mdns.so.1.0.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_mini.so.0.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_nameservice.so.0.4
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_netjson.so.1.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_p2pd.so.0.1.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_pgraph.so.1.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_pud.so.3.0.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_quagga.so.0.2.2
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_secure.so.0.6
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_sgwdynspeed.so.1.0.0
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_txtinfo.so.1.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/lib/olsrd_watchdog.so.0.1
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/sbin
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/sbin/olsrd
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/sbin/sgw_policy_routing_setup.sh
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/java
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/java/olsrd
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/java/olsrd/OlsrdPudWireFormatJava-doc.zip
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/java/olsrd/OlsrdPudWireFormatJava-src.zip
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/java/olsrd/OlsrdPudWireFormatJava.jar
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/man
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/man/man5
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/man/man5/olsrd.conf.5.gz
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/man/man8
#/home/ferry/rpmbuild/BUILDROOT/olsrd-0.9.6.2-1.fc27.x86_64/usr/share/man/man8/olsrd.8.gz




#rm -rf "%{buildroot}"
#mkdir -p "%{buildroot}/usr/sbin/"
#mkdir -p "%{buildroot}/usr/lib/"
#mkdir -p "%{buildroot}%{_initrddir}"
#mkdir -p "%{buildroot}/usr/share/man/man8"
#make DESTDIR=%{buildroot} install
#make DESTDIR=%{buildroot} install_libs
#%{__install} -m0755 olsrd.init "%{buildroot}%{_initrddir}/olsrd"


%clean
rm -rf "%{buildroot}"


%preun
#/etc/init.d/olsrd stop
#/sbin/chkconfig --del olsrd


%files
%defattr(-,root,root,-)
#%doc README CHANGELOG
#%doc lib/*/*README*

#%%config(noreplace) %%{_sysconfdir}/olsrd/olsrd.conf
#%% config %%{_initrddir}/olsrd
#/usr/sbin/olsrd
# Wildchar to cover all installed plugins
#/usr/lib/olsrd_*so*
#/usr/share/man/man8/olsrd.8.gz
#/usr/share/man/man5/olsrd.conf.5.gz


%changelog
* Tue Jul 17 2007 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created spec-file for 0.5.2

* Mon Jul 09 2007 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created spec-file for 0.5.1

* Tue Apr 03 2007 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created spec-file for 0.5.0
- Changed from INSTALL_PREFIX to DESTDIR

* Wed Jan 04 2006 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created spec-file for 0.4.10
- Removed OS=linux option to make
- Updated plugin file list, added wildchar for plugins

* Tue Apr 05 2005 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created spec-file for 0.4.9

* Tue Mar 29 2005 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Increased version number for nameservice and secure plugin

* Tue Dec 07 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Changed spec file for olsrd-0.4.8
- Removed frontend GUI inclusion
- Removed references to Unik
- Changed licence to BSD

* Tue Jun 29 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Changed spec file for unik-olsrd-0.4.5
- Remover ROOT-prefix patch as INSTALL_PREFIX is added to Makefile in 0.4.5
- Added INSTALL_PREFIX patch for front-end/Makefile
- Included plugins dot_draw and secure
- Added documentation for the plugins dyn_gw, powerinfo, dot_draw and secure

* Tue May 25 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Changed spec file for unik-olsrd-0.4.4
- Added man-page for olsrd
- Removed documentation olsrd-plugin-howto.pdf as it is no longer part of source package

* Tue Mar 02 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Changed spec file for unik-olsrd-0.4.3
- Added OLSRD plugins olsrd_dyn_gw and olsrd_power to package
- Added documentation olsrd-plugin-howto.pdf

* Tue Mar 02 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Renamed package from uolsrd to unik-olsrd to use the same name as the .deb-package
- Start olsrd daemon with option "-d 0" to start without debugging and in daemon mode
  even if debugging is enabled in olsrd.conf.

* Mon Mar 01 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Included init-script to start uolsrd daemon (installs as %{_initrddir}/uolsrd).

* Wed Feb 25 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Changed Group from Applications/System to System Environment/Daemons.
- Included olsrd-gui (forgotten in first release)
- Renamed spec file from unik-olsrd-0.4.0.spec to uolsrd-0.4.0.spec

* Wed Feb 25 2004 Roar Bjorgum Rotvik <roarbr@tihlde.org>
- Created first version of this spec-file

