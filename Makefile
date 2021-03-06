TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ARCH=$(shell uname -m)
GCC=musl-gcc
CFLAGS=-Os -static -I${TOP}/include
LDFLAGS=-static
LINUX_VER=4.0.5

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

root-mini/sbin/batctl: batctl/batctl
	cp $< $@

u9fs/u9fs: u9fs
	$(MAKE) -C u9fs CC="${GCC}" CFLAGS="${CFLAGS} -I." LD="${GCC}" LDFLAGS="${LDFLAGS}"

alfred/alfred alfred/vis/batadv-vis: alfred
	$(MAKE) -C alfred CONFIG_ALFRED_CAPABILITIES=n CONFIG_ALFRED_GPSD=n CC="${GCC}" LD="${GCC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"

linux/linux: linux linux/.config root-mini/init root-mini/sbin/u9fs root-mini/sbin/alfred root-mini/sbin/batadv-vis root-mini/sbin/socat root-mini/sbin/batctl
	$(MAKE) -C linux \
			ARCH=um \
			CC="${GCC}" \
			CFLAGS="${CFLAGS}" \
			LDFLAGS="${LDFLAGS}"

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

batctl/batctl: batctl
	$(MAKE) -C batctl CC="${GCC}" LD="${GCC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"

clean:
	$(MAKE) -C linux ARCH=um clean
	$(MAKE) -C u9fs clean
	$(MAKE) -C alfred clean
	$(MAKE) -C socat clean
	$(MAKE) -C batctl clean
	rm socat/configure || true
	rm root-mini/init || true
	rm root-mini/sbin/u9fs || true
	rm root-mini/sbin/alfred || true
	rm root-mini/sbin/batadv-vis || true
	rm root-mini/sbin/socat || true
	rm root-mini/sbin/batctl || true

linux/.config: linux.config linux
	cp linux.config $@

kernel-headers:
	git clone git://github.com/sabotage-linux/kernel-headers

include: kernel-headers
	$(MAKE) -C kernel-headers ARCH="${ARCH}" prefix="${TOP}" install

u9fs:
	hg clone https://bitbucket.org/plan9-from-bell-labs/u9fs
	cd u9fs && for p in ../patches/u9fs/*; do patch -p1 < $$p; done

alfred:
	git clone http://git.open-mesh.org/alfred.git

socat:
	git clone git://repo.or.cz/socat.git
	cd socat && git checkout fixes && for p in ../patches/socat/*; do patch -p1 < $$p; done

batctl:
	git clone git://git.open-mesh.org/batctl.git
	cd batctl && git checkout v2013.4.0 && for p in ../patches/batctl-2013.4.0/*; do patch -p1 < $$p; done

linux:
	[ -d linux-${LINUX_VER} ] || wget -O - https://www.kernel.org/pub/linux/kernel/v4.x/linux-${LINUX_VER}.tar.xz | tar xJf -
	cd linux-${LINUX_VER} && for p in ../patches/linux/0*; do patch -p1 < $$p; done
	ln -s linux-${LINUX_VER} linux

prepare: linux alfred u9fs include

prepare-v14: prepare
	[ ! -d linux/net/batman-adv-legacy ] && cd linux && patch -p1 < ../patches/linux/linux-add-batman-legacy.patch

prepare-v15: prepare
	[ -d linux/net/batman-adv-legacy ] && cd linux && patch -p1 -R < ../patches/linux/linux-add-batman-legacy.patch
