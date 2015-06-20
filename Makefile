GCC=musl-gcc
CFLAGS=-Os -static
LDFLAGS=-static
LINUX_VER=4.0.5

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

all: linux/linux

config: linux/.config
	$(MAKE) -C linux ARCH=um menuconfig
	cp ${TOP}/linux/.config ${TOP}/linux.config

root-mini/init: src/init.c
	${GCC} -pipe ${CFLAGS} ${LDFLAGS} -o $@ $<

root-mini/sbin/u9fs: u9fs/u9fs
	cp $< $@

root-mini/sbin/alfred: alfred/alfred
	cp $< $@

root-mini/sbin/batadv-vis: alfred/vis/batadv-vis
	cp $< $@

root-mini/sbin/socat: socat/socat
	cp $< $@

u9fs/u9fs: u9fs
	$(MAKE) -C u9fs CC="${GCC}" CFLAGS="${CFLAGS} -I." LD="${GCC}" LDFLAGS="${LDFLAGS}"

alfred/alfred alfred/vis/batadv-vis: alfred
	$(MAKE) -C alfred CONFIG_ALFRED_CAPABILITIES=n CONFIG_ALFRED_GPSD=n CC="${GCC}" LD="${GCC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"

linux/linux: linux linux/.config root-mini/init root-mini/sbin/u9fs root-mini/sbin/alfred root-mini/sbin/batadv-vis root-mini/sbin/socat vde/vde-2/src/lib/.libs/libvdeplug.a
	$(MAKE) -C linux \
			ARCH=um \
			CC="${GCC} ${CFLAGS}" \
			CFLAGS="-I${TOP}/vde/vde-2/include -L${TOP}/vde/vde-2/src/lib/.libs" \
			LDFLAGS_vde.o="-r ${TOP}/vde/vde-2/src/lib/.libs/libvdeplug.a"

socat/configure.ac: socat

socat/configure: socat/configure.ac
	cd socat && autoreconf --install || true

socat/Makefile: socat/configure
	cd socat && \
			CC="${GCC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}" LD="${GCC}" \
			./configure \
				--disable-help --disable-termios --disable-sctp \
				--disable-socks4 --disable-socks4a --disable-system \
				--disable-pty --disable-ext2 --disable-readline \
				--disable-openssl --disable-tun --disable-sycls \
				--disable-filan --disable-libwrap

socat/socat: socat/Makefile
	$(MAKE) -C socat socat

clean:
	$(MAKE) -C linux ARCH=um clean
	[ -f vde/vde2/Makefile ] && $(MAKE) -C vde/vde-2 clean || true
	$(MAKE) -C u9fs clean
	$(MAKE) -C alfred clean
	$(MAKE) -C socat clean
	rm root-mini/init
	rm root-mini/sbin/u9fs
	rm root-mini/sbin/alfred
	rm root-mini/sbin/batadv-vis
	rm root-mini/sbin/socat

build-vde vde/vde-2/src/lib/.libs/libvdeplug.a: vde
	[ -f vde/vde-2/configure ] || cd vde/vde-2 && autoreconf --install
	cd vde/vde-2 && \
			CC="${GCC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}" \
			./configure --disable-cryptcab --disable-vde_over_ns --disable-python --disable-router
	$(MAKE) -C vde/vde-2/src/common
	$(MAKE) -C vde/vde-2/src/lib
	$(MAKE) -C vde/vde-2/src/vde_switch

linux/.config: linux.config linux
	cp linux.config $@

vde:
	svn checkout svn://svn.code.sf.net/p/vde/svn/trunk vde

u9fs:
	hg clone https://bitbucket.org/plan9-from-bell-labs/u9fs
	cd u9fs && for p in ../patches/u9fs/*; do patch -p1 < $$p; done

alfred:
	git clone http://git.open-mesh.org/alfred.git

socat:
	git clone git://repo.or.cz/socat.git
	cd socat && git checkout fixes && for p in ../patches/socat/*; do patch -p1 < $$p; done

linux:
	[ -d linux-${LINUX_VER} ] || wget -O - https://www.kernel.org/pub/linux/kernel/v4.x/linux-${LINUX_VER}.tar.xz | tar xJf -
	cd linux-${LINUX_VER} && for p in ../patches/linux/0*; do patch -p1 < $$p; done
	ln -s linux-${LINUX_VER} linux

prepare: linux vde alfred u9fs

prepare-v14: prepare
	cd linux && patch -p1 < ../patches/linux/linux-add-batman-legacy.patch
