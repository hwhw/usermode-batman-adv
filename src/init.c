#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <linux/sockios.h>


/* listen on this port for 9P filesystem client connections */
#define SERVER9P_PORT 564

int sockfd;
char* service_ip = NULL;
char* service_netmask = NULL;

#define ERROR(STR...) { printf(STR); exit(-1); }
#define MAX_DEV 999

/*
 * bring an interface up
 */
void if_up(const char *interface) {
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ);

    if(ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0)
        ERROR("can't get interface flags for %s\n", interface);

    ifr.ifr_flags = ifr.ifr_flags | IFF_UP;

    if(ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0)
        ERROR("can't set interface flags for %s\n", interface);
}

/*
 * set an interface's IPv4 address
 */
void if_set_ip(const char *interface, const char *address) {
    struct sockaddr_in sai;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ);

    memset(&sai, 0, sizeof(struct sockaddr));
    sai.sin_family = AF_INET;
    sai.sin_port = 0;
    sai.sin_addr.s_addr = inet_addr(address);
    memcpy(&ifr.ifr_addr, &sai, sizeof(sai));
    if (ioctl(sockfd, SIOCSIFADDR, &ifr) < 0)
        ERROR("can't set IPv4 address %s for %s\n", address, interface);
}

/*
 * set an interface's hardware address (MAC address)
 */
void if_set_hwaddr(const char *interface, const char *address) {
    struct ifreq ifr;
    unsigned int mac[6];
    int i;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ);
    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0)
        ERROR("can't get hardware address for %s: %s (%d)\n", interface, strerror(errno), errno);

    memcpy(ifr.ifr_hwaddr.sa_data, ether_aton(address), sizeof(struct ether_addr));

    if (ioctl(sockfd, SIOCSIFHWADDR, &ifr) < 0)
        ERROR("can't set hardware address %s for %s: %s (%d)\n", address, interface, strerror(errno), errno);
}

/*
 * set an interface's MTU
 */
void if_set_mtu(const char *interface, int mtu) {
    struct ifreq ifr;
    int i;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ);
    ifr.ifr_mtu = mtu;

    if (ioctl(sockfd, SIOCSIFMTU, &ifr) < 0)
        ERROR("can't set MTU %d for %s: %s (%d)\n", mtu, interface, strerror(errno), errno);
}

/*
 * create a L2 bridge device
 */
void br_addbr(const char *bridge) {
    if(ioctl(sockfd, SIOCBRADDBR, bridge) < 0)
        ERROR("cannot create bridge: %s (%d)\n", strerror(errno), errno);
}

/*
 * add an interface to a bridge
 */
void br_addif(const char *bridge, const char *interface) {
    unsigned int ifidx;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    ifidx = if_nametoindex(interface);
    if (ifidx == 0)
        ERROR("did not find interface %s\n", interface);

    ifr.ifr_ifindex = ifidx;
    strncpy(ifr.ifr_name, bridge, IFNAMSIZ);

    if (ioctl(sockfd, SIOCBRADDIF, &ifr) < 0)
        ERROR("cannot add interface %s to bridge %s: %s (%d)\n", interface, bridge, strerror(errno), errno);
}

/*
 * write a string to a file
 */
void writestring(const char* file, const char *data) {
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        ERROR("cannot open file %s for writing: %s (%d)\n", file, strerror(errno), errno);
    write(fd, data, strlen(data));
    close(fd);
}

/*
 * double-fork a child process
 */
void fork_process(const char *path, char *const argv[]) {
    pid_t child, child2;
    int status;

    child = fork();
    if(child == -1) {
        ERROR("error forking for child process: %s (%d)\n", strerror(errno), errno);
    } else if(child == 0) {
        while(1) {
            child2 = fork();
            if(child2 == -1) {
                ERROR("error forking for real child process: %s (%d)\n", strerror(errno), errno);
            } else if(child2 == 0) {
                execv(path, argv);
                ERROR("server error when starting child process: %s (%d)\n", strerror(errno), errno);
            } else {
                do {
                    waitpid(child2, &status, 0);
                } while(!WIFEXITED(status));
                printf("child process exited, restarting in 1 second.\n");
                sleep(1);
            }
        }
    }
}

/*
 * configuration: helper for config items
 */
void early_config(char *token, char *value) {
    char *alfred_slave[] = {"alfred", "-i", "bat0", "-u", "/alfred.sock", NULL};
    char *alfred_master[] = {"alfred", "-m", "-i", "bat0", "-u", "/alfred.sock", NULL};
    char *batadv_vis[] = {"batadv-vis", "-i", "bat0", "-u", "/alfred.sock", "-s", NULL};

    if (!strcmp("service_ip", token)) {
        service_ip = strdup(value);
    } else if (!strcmp("service_netmask", token)) {
        service_netmask = strdup(value);
    } else if (!strcmp("bat0_hwaddr", token)) {
        if_set_hwaddr("bat0", value);
    } else if (!strncmp("mtu_eth", token, 7)) {
        if_set_mtu(token + 4, atoi(value));
    }
}
void late_config(char *token, char *value) {
    char *alfred_slave[] = {"alfred", "-i", "bat0", "-u", "/alfred.sock", NULL};
    char *alfred_master[] = {"alfred", "-i", "bat0", "-u", "/alfred.sock", "-m", NULL};
    char *batadv_vis[] = {"batadv-vis", "-i", "bat0", "-u", "/alfred.sock", "-s", NULL};

    if (!strcmp("run_alfred", token)) {
        if(!strcmp("master", value)) {
            fork_process("/sbin/alfred", alfred_master);
        } else if (!strcmp("slave", value)) {
            fork_process("/sbin/alfred", alfred_slave);
        }
        fork_process("/sbin/batadv-vis", batadv_vis);
    } else if (token[0] == '/') {
        writestring(token, value);
    }
}

/*
 * configuration parsing
 *
 * reads kernel command line arguments and passes name/value pairs
 * to do_config_item().
 */
void do_config(void (*helper)(char *, char *)) {
    char buf[1024];
    char *val;
    int p, n, fd;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0)
        ERROR("cannot open /proc/cmdline for writing: %s (%d)\n", strerror(errno), errno);
    p = 0;
    while (1) {
        n = read(fd, buf+p, 1);
        if(n == 0 || buf[p] == ' ' || buf[p] == '\n' || buf[p] == '\0' || buf[p] == '\t') {
            if(p == 0) {
                if(n == 0) { break; } else { continue; }
            }
            buf[p] = '\0';
            (*helper)(buf, val);
            val = buf;
            p = 0;
            if(n == 0) break;
        } else {
            if(p > 0 && buf[p] == '=') {
                buf[p] = '\0';
                val = &buf[p + 1];
            }
            p++;
            if(p == 1023) {
                if(val != buf) {
                    printf("parameter %s too large, cutting.\n", buf);
                    p = (val - buf);
                } else {
                    printf("parameter too large, cutting.\n");
                    p = 0;
                }
            }
        }
    }
    close(fd);
}

/*
 * this is a bit as if we were becoming an inetd server for a 9P daemon
 *
 * this function will listen on a port and wait for connections.
 * On connection, it is accepted and a child process is spawned to
 * handle it.
 */
void run_9p_server() {
    struct sockaddr_in sai;
    int server_fd;
    int conn_fd;
    pid_t child;
    int reuse_addr = 1;

    memset(&sai, 0, sizeof(struct sockaddr));
    sai.sin_family = AF_INET;
    sai.sin_port = htons(SERVER9P_PORT);
    sai.sin_addr.s_addr = inet_addr(service_ip);

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == -1)
        ERROR("cannot open server socket: %s (%d)\n", strerror(errno), errno);

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == -1)
        ERROR("cannot set server socket flags: %s (%d)\n", strerror(errno), errno);

    if (bind(server_fd, (struct sockaddr *) &sai, sizeof(struct sockaddr_in)) == -1)
        ERROR("cannot bind server socket: %s (%d)\n", strerror(errno), errno);

    if (listen(server_fd, SOMAXCONN))
        ERROR("cannot listen on server socket: %s (%d)\n", strerror(errno), errno);

    signal(SIGCHLD, SIG_IGN);

    while (1) {
        conn_fd = accept(server_fd, 0, 0);
        if(conn_fd == -1) {
            if(errno == EINTR) continue;
            ERROR("server error accepting connection: %s (%d)\n", strerror(errno), errno);
        }
        child = fork();
        if(child == -1) {
            ERROR("server error forking for connection: %s (%d)\n", strerror(errno), errno);
        } else if(child == 0) {
            close(server_fd);
            close(0);
            close(1);
            if(dup2(conn_fd, 0) != -1 && dup2(conn_fd, 1) != -1) {
                execl(
                    "/sbin/u9fs", "u9fs",
                    "-a", "none",
                    "-l", "/dev/null",
                    "-u", "root",
                    (char  *) NULL);
            }
            ERROR("server error when starting child process: %s (%d)\n", strerror(errno), errno);
        } else {
            close(conn_fd);
        }
    }
}

#define TPL_MESH_IFACE "/sys/class/net/%s/batman_adv/mesh_iface"
#define MAX_LEN_IFACE sizeof("eth999")
#define MAX_LEN_MESH_IFACE (sizeof(TPL_MESH_IFACE)+sizeof("eth999"))

int main(int argc, char* argv[]) {
    int fd, n;
    char *errstr;
    char *dev;
    char *confpath;

    /*
     * standard mount points
     */
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sys", "/sys", "sysfs", 0, NULL);
    mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);

    /*
     * use /dev/tty0 for input/output
     */
    fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) return -1;
    close(1);
    close(2);
    dup2(fd, 1);
    dup2(fd, 2);

    /*
     * configuration socket for network config
     */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        ERROR("cannot open network socket: %s (%d)\n", strerror(errno), errno);

    /*
     * create a bridge that connects UML-external interface to
     * the batman interface (added later on)
     */
    br_addbr("bat0br");
    br_addif("bat0br", "eth1");

    /*
     * now, all interfaces eth2, eth3, ethN (at least eth2 must be there)
     * are brought under control of batman-adv and then brought up.
     */
    if (!if_nametoindex("eth2"))
        ERROR("at least one interface to put batman-adv onto must be specified (eth1)\n");

    dev = malloc(MAX_LEN_IFACE);
    confpath = malloc(MAX_LEN_MESH_IFACE);
    if (dev == NULL || confpath == NULL)
        ERROR("cannot allocate memory\n");

    for (n=2; n <= MAX_DEV; n++) {
        snprintf(dev, MAX_LEN_IFACE, "eth%d", n);
        if(!if_nametoindex(dev)) break;
        snprintf(confpath, MAX_LEN_MESH_IFACE, TPL_MESH_IFACE, dev);
        writestring(confpath, "bat0");
        if_up(dev);
        printf("added device %s to the mesh (batman-adv is taking care of it).\n", dev);
    }

    do_config(early_config);

    br_addif("bat0br", "bat0");

    if (service_ip == NULL)
        ERROR("need service_ip parameter!\n");

    if_set_ip("eth0", service_ip);

    if_up("eth0");
    if_up("eth1");
    if_up("bat0");
    if_up("bat0br");

    do_config(late_config);

    close(sockfd);

    run_9p_server();
    ERROR("what am I doing here? %d / %d / %s\n", n, errno, strerror(errno));
}
