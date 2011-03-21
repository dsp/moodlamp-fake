#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#define NAME "moodlamp"
#define SERVICE "_moodlamp._udp"
#define PORT 2323

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
void create_service(AvahiClient * c)
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
                                                 AVAHI_PROTO_INET6, 0,
                                                 name, SERVICE,
                                                 NULL, NULL, PORT,
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
    } else {
        serv();
    }
}

static
void client_callback(AvahiClient * c, AvahiClientState s, void *userdata)
{
    switch (s) {
    case AVAHI_CLIENT_S_RUNNING:
        create_service(c);
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

static void set(char b1, char b2, char b3)
{
    fprintf(stderr, "set to %x %x %x\n", b1, b2, b3);
}

static void fade(char b1, char b2, char b3, char b4)
{
    fprintf(stderr, "fade to %x %x %x in %ds\n", b1 & 0xFF, b2 & 0xFF,
            b3 & 0xFF, b4);
}

static int serv(void)
{
    int sock;
    struct sockaddr_in6 addr;
    struct sockaddr_in6 caddr;
    socklen_t caddrlen;

    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "cannot create socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin6_port = htons(PORT);
    addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
        fprintf(stderr, "cannot bind");
        return -1;
    }
    while (1) {
        char command[6];
        fprintf(stderr, "wait..\n");
        if (recvfrom
            (sock, &command, 6, 0, (struct sockaddr *) &caddr,
             &caddrlen) < 6) {

            fprintf(stderr, "not a ASCII moodlamp command");
            continue;
        }
        switch (command[0]) {
        case 'C':
            set(command[1], command[2], command[3]);
            break;
        case 'F':
        case 'M':
        case 'T':
            fade(command[1], command[2], command[3], command[5]);
            break;
        default:
            fprintf(stderr, "not a ASCII moodlamp command");
        }

        fprintf(stderr, "receive\n");

    }

    close(sock);
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
