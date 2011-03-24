#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#define NAME "moodlamp"
#define SERVICE "_moodlamp._udp"
#define UDP_PORT 2323
#define TCP_PORT 2324

AvahiEntryGroup *group = NULL;
AvahiSimplePoll *sp = NULL;
char *name;

static int serv();

static
void entry_group_callback(AvahiEntryGroup * g, AvahiEntryGroupState state,
                          AVAHI_GCC_UNUSED void *userdata)
{
    group = g;

    /* Called whenever the entry group state changes */
    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        /* The entry group has been established successfully */
        fprintf(stderr, "Service '%s' successfully established.\n", name);
        serv();
        break;

    case AVAHI_ENTRY_GROUP_COLLISION:{
            fprintf(stderr,
                    "Service name collision, renaming service to '%s'\n",
                    name);
            avahi_simple_poll_quit(sp);
            break;
        }

    case AVAHI_ENTRY_GROUP_FAILURE:
        fprintf(stderr, "Entry group failure: %s\n",
                avahi_strerror(avahi_client_errno
                               (avahi_entry_group_get_client(g))));
        /* Some kind of failure happened while we were registering our services */
        avahi_simple_poll_quit(sp);
        break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
        ;
    }
}

static
void create_service(AvahiClient * c, const char * service, int port)
{
    int ret;

    if (!group) {
        group = avahi_entry_group_new(c, entry_group_callback, NULL);
        if (!group) {
            fprintf(stderr, "Cannot create new entry group");
            avahi_simple_poll_quit(sp);
            return;
        }
    }

    if (avahi_entry_group_is_empty(group)) {
        /* Add the service for IPP */
        if ((ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC,
                                                 AVAHI_PROTO_UNSPEC, 0,
                                                 name, service,
                                                 NULL, NULL, port,
                                                 NULL)) < 0) {

            if (ret == AVAHI_ERR_COLLISION) {
                fprintf(stderr, "Service name collision");
                avahi_simple_poll_quit(sp);
                return;
            }

            fprintf(stderr, "Failed to add _moodlamp._udp service: %s\n",
                    avahi_strerror(ret));
            avahi_simple_poll_quit(sp);
            return;
        }

        /* Tell the server to register the service */
        if ((ret = avahi_entry_group_commit(group)) < 0) {
            fprintf(stderr, "Failed to commit entry group: %s\n",
                    avahi_strerror(ret));
            avahi_simple_poll_quit(sp);
            return;
        }
    }
}

static
void client_callback(AvahiClient * c, AvahiClientState s, void *userdata)
{
    switch (s) {
    case AVAHI_CLIENT_S_RUNNING:
        create_service(c, "_moodlamp._udp", UDP_PORT);
        create_service(c, "_moodlamp._tcp", TCP_PORT);
        break;
    case AVAHI_CLIENT_FAILURE:
        avahi_simple_poll_quit(sp);
        break;
    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
        if (group) {
            avahi_entry_group_reset(group);
        }
    case AVAHI_CLIENT_CONNECTING:
        break;
    }
}

static void set(int b1, int b2, int b3)
{
    fprintf(stderr, "set to %02x %02x %02x\n", b1, b2, b3);
}

static void fade(int b1, int b2, int b3, unsigned long ms)
{
    fprintf(stderr, "fade to %02x %02x %02x in %ldms\n", b1, b2, b3, ms);
}

static void exec_command(char *command, ssize_t len, void *retbuf,
                         ssize_t *retlen)
{
    unsigned short *numbs;

    numbs = alloca(sizeof(unsigned short) * len);
    memset(numbs, 0, len);
    for (int i = 0; i < len && command[i] != '0'; i++) {
        numbs[i] = command[i] & 0xFF;
    }


    if (numbs[0] == 'B') {
        for (int i = 0; i < len-2; i++) {
            numbs[i] = numbs[i+2];
        }
    }

    switch (numbs[0]) {
    case 'C':
        set(numbs[1], numbs[2], numbs[3]);
        break;
    case 'F':
    case 'M':
    case 'T':
        if (len >= 6) {
            unsigned int time = 0;
            time = numbs[4] << 8;
            time += numbs[5] && 0xFF;
            fade(numbs[1], numbs[2], numbs[3], time);
        } else {
            fprintf(stderr, "not a ASCII moodlamp command");
        }
        break;
    case 'V':
        break;
    default:
        fprintf(stderr, "not a ASCII moodlamp command\n");
    }
}

static void handle_tcp(int sock) {
    char command[6];
    void * retbuf = NULL;
    ssize_t retlen, s;
    struct sockaddr_in6 caddr;
    socklen_t caddrlen;

    int fd = accept(sock, &caddr, &caddrlen);
    if ((s = read(fd, command, 6)) < 0) {
        fprintf(stderr, "not a ASCII moodlamp command\n");
        return;
   }

    exec_command(command, strlen(command), retbuf, &retlen);
    write(fd, command, retlen);
    close(fd);
}

static void handle_udp(int sock)
{
    size_t s;
    char command[6];
    struct sockaddr_in6 caddr;
    socklen_t caddrlen;

    if ((s = recvfrom
         (sock, command, 6, 0, (struct sockaddr *) &caddr,
          &caddrlen)) < 4) {
        fprintf(stderr, "not a ASCII moodlamp command");
        return;
    }
    exec_command(command, s, NULL, NULL);
}

static int serv(void)
{
    struct pollfd fds[2];
    int udp_sock, tcp_sock;
    struct sockaddr_in6 tcp_addr;
    struct sockaddr_in6 udp_addr;

    udp_sock = socket(AF_INET6, SOCK_DGRAM, 0);
    tcp_sock = socket(AF_INET6, SOCK_STREAM, 0);

    if (udp_sock < 0) {
        fprintf(stderr, "cannot create udp socket");
        return -1;
    }

    if (tcp_sock < 0) {
        fprintf(stderr, "cannot create udp socket");
        return -1;
    }

    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin6_port = htons(UDP_PORT);
    udp_addr.sin6_addr = in6addr_any;

    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin6_port = htons(TCP_PORT);
    tcp_addr.sin6_addr = in6addr_any;

    if (bind(udp_sock, (struct sockaddr *) &udp_addr, sizeof(udp_addr))) {
        fprintf(stderr, "cannot bind udp socket");
        return -1;
    }

    if (bind(tcp_sock, (struct sockaddr *) &tcp_addr, sizeof(tcp_addr))) {
        fprintf(stderr, "cannot bind tcp socket");
        return -1;
    }

    fds[0].fd = udp_sock;
    fds[1].fd = tcp_sock;
    fds[0].events = POLLIN | POLLPRI;
    fds[1].events = POLLIN | POLLPRI;
    fds[0].revents = 0;
    fds[1].revents = 0;

    listen(tcp_sock, 10);
    while (1) {
        if (poll(fds, 2, 1000) <= 0) {
            continue;
        }

        fprintf(stderr, "socket ready\n");
        for (int sockn = 0; sockn < 2; sockn++) {
            if (fds[sockn].revents && fds[sockn].fd == tcp_sock) {
                handle_tcp(tcp_sock);
            } else if (fds[sockn].revents && fds[sockn].fd == udp_sock) {
                handle_udp(udp_sock);
            }
        }
    }
}

int main(AVAHI_GCC_UNUSED int argc, AVAHI_GCC_UNUSED char *argv[])
{
    AvahiClient *client = NULL;
    int error, ret = 0;

    name = avahi_strdup(NAME);
    sp = avahi_simple_poll_new();
    if (!sp) {
        fprintf(stderr, "Cannot instantiate simple poll");
        goto cleanup;
    }

    client =
        avahi_client_new(avahi_simple_poll_get(sp), 0, client_callback,
                         NULL, &error);
    if (!client) {
        fprintf(stderr, "Cannot instanitate client");
        goto cleanup;
    }

    avahi_simple_poll_loop(sp);

    ret = 1;

  cleanup:
    if (client) {
        avahi_client_free(client);
    }

    if (sp) {
        avahi_simple_poll_free(sp);
    }
    avahi_free(name);

    return ret;
}
