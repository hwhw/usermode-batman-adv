# batman-adv in userland

This is an approach to use B.A.T.M.A.N. advanced (http://www.open-mesh.org/projects/batman-adv/wiki/Wiki)
in userland. This is not easy, since it is designed as a driver for the Linux kernel.
So the approach taken here is simple - have a Linux kernel run in userland.

Thankfully, this is already possible using User Mode Linux (UML). While you don't see exactly
new information about this feature of the Linux kernel, it is still there, waiting to be used.

## Features

Comes with integration of
* A.L.F.R.E.D. (http://www.open-mesh.org/projects/alfred) and batadv-vis
  allows to publish/collect broadcast information from the Mesh network
* socat is used to provide access to A.L.F.R.E.D.'s Unix socket interface
  via TCP (see below)
* u9fs (https://bitbucket.org/plan9-from-bell-labs/u9fs)
  allows access to the UML-internal file system

Sports a small init program that sets up everything that's needed. No additional shell provided.

## Restrictions

User Mode Linux will run only on a subset of architectures the Linux kernel can be run on natively.

## Building

```bash
make prepare
make
cp linux/linux /usr/local/bin/linux-batman-v15
```

and/or

```bash
make prepare-v14
make
cp linux/linux /usr/local/bin/linux-batman-v14
```

## Running and configuring

```bash
ip tuntap add dev batu0 mode tap user nobody group nogroup
ip link set batu0 up

ip tuntap add dev batu0-mgmt mode tap user nobody group nogroup
ip address add 192.168.254.1/24 dev batu0-mgmt

ip link set batu0-mgmt up
ip tuntap add dev fastd1-batu0 mode tap user nobody group nogroup
ip link set fastd1-batu0 up
ip tuntap add dev fastd2-batu0 mode tap user nobody group nogroup
ip link set fastd2-batu0 up
ip tuntap add dev fastd3-batu0 mode tap user nobody group nogroup
ip link set fastd3-batu0 up
ip tuntap add dev fastd4-batu0 mode tap user nobody group nogroup
ip link set fastd4-batu0 up
ip tuntap add dev vpn1-batu0 mode tap user nobody group nogroup
ip link set vpn1-batu0 up
ip tuntap add dev vpn2-batu0 mode tap user nobody group nogroup
ip link set vpn2-batu0 up

TEMP=/tmp start-stop-daemon -b -c nobody:nogroup --start \
                --exec /usr/local/bin/linux-batman-v14 -- \
                umid=linux-batman-v14 uml_dir=/run/uml \
                eth0=tuntap,batu0-mgmt \
                eth1=tuntap,batu0,ca:fe:fe:fe:03:f1 \
                eth2=tuntap,fastd1-batu0,ca:fe:fe:fe:03:e1 \
                eth3=tuntap,fastd2-batu0,ca:fe:fe:fe:03:e2 \
                eth4=tuntap,fastd3-batu0,ca:fe:fe:fe:03:e3 \
                eth5=tuntap,fastd4-batu0,ca:fe:fe:fe:03:e4 \
                eth6=tuntap,vpn1-batu0,ca:fe:fe:fe:03:ea \
                eth7=tuntap,vpn2-batu0,ca:fe:fe:fe:03:eb \
                mtu_eth2=1408 \
                mtu_eth3=1408 \
                mtu_eth4=1408 \
                mtu_eth5=1408 \
                mtu_eth6=1462 \
                mtu_eth7=1462 \
                bat0_hwaddr=ca:fe:fe:fe:03:f0 \
                service_ip=192.168.254.2 \
                run_alfred=slave \
                run_u9fs \
                /sys/class/net/bat0/mesh/gw_mode=server \
                /sys/class/net/bat0/mesh/gw_bandwidth=1024MBit/1024MBit

[ -d /mnt/batman-v14 ] || mkdir /mnt/batman-v14
sleep 10 && mount -t 9p 192.168.254.2 /mnt/batman-v14
```

This example sets up a tap interface `batu0-mgmt` that allows IPv4 based traffic between host and UML instance. Within the UML instance, the IP address is set to the address given on the kernel command line in the ```service_ip``` parameter. On this address, a u9fs server will be listening that provides access to the file system root of the UML instance. You can mount this from the host and access the UML instance's file system. This interface is configured as `eth0` for the UML kernel.

It also sets up a tap interface `batu0` that roughly corresponds to the `bat0` interface you would expect from running batman-adv in kernel. This interface is always configured as the second interface of the UML kernel, `eth1`.

It also sets up a bunch of further interfaces, starting at `eth2`. All these interfaces will be brought up and batman-adv will be activated upon them.

Further config flags exist for setting the address of the `bat0` device within the UML instance and the MTU of the available interfaces within the UML instance.

You can also configure whether to run the A.L.F.R.E.D. daemon, either in slave mode (`run_alfred=slave`) or in master mode (`run_alfred=master`).

If you do not specify `run_u9fs` or `run_alfred`, the corresponding daemons are not run at all.

When you pass kernel parameters whose names start with a `/`, they are interpreted as file names and the value of the parameter is written to the according files within the UML instance, after setting up networking. Use the 9P file system access for more elaborate access.

You can use the `alfred` tool outside the UML instance. In the needed Unix socket can be provided e.g. via socat, which will forward the socket requests via TCP into the UML instance, where another socat instance does the reverse (example uses the service_ip configured in the example above):
```
socat UNIX-LISTEN:/var/run/alfred.sock,fork TCP:192.168.254.2:16962 &
alfred -r 158
```
