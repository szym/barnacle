#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>


#define ANDROID_SOCKET_ENV_PREFIX "ANDROID_SOCKET_"
#define ANDROID_SOCKET_DIR    "/dev/socket"

// Linux "abstract" (non-filesystem) namespace
#define ANDROID_SOCKET_NAMESPACE_ABSTRACT 0
// Android "reserved" (/dev/socket) namespace
#define ANDROID_SOCKET_NAMESPACE_RESERVED 1
// Normal filesystem namespace
#define ANDROID_SOCKET_NAMESPACE_FILESYSTEM 2


#define FILESYSTEM_SOCKET_PREFIX "/tmp/"
#define ANDROID_RESERVED_SOCKET_PREFIX "/dev/socket/"

int socket_make_sockaddr_un(const char *name, int namespaceId,
        struct sockaddr_un *p_addr, socklen_t *alen)
{
    memset (p_addr, 0, sizeof (*p_addr));
    size_t namelen;

    switch (namespaceId) {
        case ANDROID_SOCKET_NAMESPACE_ABSTRACT:
#ifdef HAVE_LINUX_LOCAL_SOCKET_NAMESPACE
            namelen  = strlen(name);

            // Test with length +1 for the *initial* '\0'.
            if ((namelen + 1) > sizeof(p_addr->sun_path)) {
                goto error;
            }

            /*
             * Note: The path in this case is *not* supposed to be
             * '\0'-terminated. ("man 7 unix" for the gory details.)
             */

            p_addr->sun_path[0] = 0;
            memcpy(p_addr->sun_path + 1, name, namelen);
#else /*HAVE_LINUX_LOCAL_SOCKET_NAMESPACE*/
            /* this OS doesn't have the Linux abstract namespace */

            namelen = strlen(name) + strlen(FILESYSTEM_SOCKET_PREFIX);
            /* unix_path_max appears to be missing on linux */
            if (namelen > sizeof(*p_addr)
                    - offsetof(struct sockaddr_un, sun_path) - 1) {
                goto error;
            }

            strcpy(p_addr->sun_path, FILESYSTEM_SOCKET_PREFIX);
            strcat(p_addr->sun_path, name);
#endif /*HAVE_LINUX_LOCAL_SOCKET_NAMESPACE*/
        break;

        case ANDROID_SOCKET_NAMESPACE_RESERVED:
            namelen = strlen(name) + strlen(ANDROID_RESERVED_SOCKET_PREFIX);
            /* unix_path_max appears to be missing on linux */
            if (namelen > sizeof(*p_addr)
                    - offsetof(struct sockaddr_un, sun_path) - 1) {
                goto error;
            }

            strcpy(p_addr->sun_path, ANDROID_RESERVED_SOCKET_PREFIX);
            strcat(p_addr->sun_path, name);
        break;

        case ANDROID_SOCKET_NAMESPACE_FILESYSTEM:
            namelen = strlen(name);
            /* unix_path_max appears to be missing on linux */
            if (namelen > sizeof(*p_addr)
                    - offsetof(struct sockaddr_un, sun_path) - 1) {
                goto error;
            }

            strcpy(p_addr->sun_path, name);
        break;
        default:
            // invalid namespace id
            return -1;
    }

    p_addr->sun_family = AF_LOCAL;
    *alen = namelen + offsetof(struct sockaddr_un, sun_path) + 1;
    return 0;
error:
    return -1;
}




/**
 * connect to peer named "name" on fd
 * returns same fd or -1 on error.
 * fd is not closed on error. that's your job.
 *
 * Used by AndroidSocketImpl
 */
int socket_local_client_connect(int fd, const char *name, int namespaceId)
{
    struct sockaddr_un addr;
    socklen_t alen;
    int err;

    err = socket_make_sockaddr_un(name, namespaceId, &addr, &alen);

    if (err < 0) {
        goto error;
    }

    if(connect(fd, (struct sockaddr *) &addr, alen) < 0) {
        goto error;
    }

    return fd;

error:
    return -1;
}

int
socket_create (bool stream)
{
    int ret;

    ret = socket(PF_LOCAL, stream ? SOCK_STREAM : SOCK_DGRAM, 0);

    if (ret < 0) {
        return -1;
    }

    return ret;
}

#define ERR fprintf(stderr, "error in line %d : %s", __LINE__, strerror(errno))
int main(int argc, const char *argv[]) {
  if (argc < 2) return -1;
  int fd = socket_create(true);
  if (fd < 0) ERR;
  else {
    int ret = socket_local_client_connect(fd, argv[1], ANDROID_SOCKET_NAMESPACE_FILESYSTEM);
    if (ret < 0) ERR;
    else {
      fprintf(stderr, "connected!\n");
    }
  }
  return 0;
}
