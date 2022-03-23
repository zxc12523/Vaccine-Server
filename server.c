#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) \
    do              \
    {               \
        perror(a);  \
        exit(1);    \
    } while (0)

typedef struct
{
    char hostname[512];  // server's hostname
    unsigned short port; // port to listen
    int listen_fd;       // fd to wait for a new connection
} server;

typedef struct
{
    char host[512]; // client's host
    int conn_fd;    // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len; // bytes used by buf
    // you don't need to change this.
    int id;
    int wait_for_write; // used by handle_read to know if the header is read or not.
} request;

typedef struct
{
    int id; // 902001-902020
    int AZ;
    int BNT;
    int Moderna;
} registerRecord;

server svr;               // server
request *requestP = NULL; // point to a list of requests
int maxfd;                // size of open file descriptor table, size of request list

int write_locked[20];

const char *accept_read_header = "ACCEPT_FROM_READ";
const char *accept_write_header = "ACCEPT_FROM_WRITE";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request *reqP);
// initailize a request instance

static void free_request(request *reqP);
// free resources used by a request instance

int handle_read(request *reqP)
{
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0)
        return -1;
    if (r == 0)
        return 0;
    char *p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    if (p1 == NULL)
    {
        p1 = strstr(buf, "\012");
        if (p1 == NULL)
        {
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len - 1;
    return 1;
}

void set_fl(int fd, int flag)
{
    int val;
    if (val = fcntl(fd, F_GETFL, 0) < 0)
        fprintf(stderr, "get flag err\n");

    val |= flag;

    if (fcntl(fd, F_SETFL, val) < 0)
        fprintf(stderr, "set flag err\n");
}

int check_id(int id)
{
    if (902001 <= id && id <= 902020)
        return 1;
    else
        return 0;
}

int check_order(char *s, int l)
{
    if (l != 5 || s[1] != ' ' || s[3] != ' ' || (s[0] <= '0' || '4' <= s[0]) || (s[2] <= '0' || '4' <= s[2]) || (s[4] <= '0' || '4' <= s[4]) || (s[0] == s[2] || s[2] == s[4] || s[4] == s[0]))
        return 0;
    else
        return 1;
}

int query(int fd, request *reqP, int pos, struct flock *lock, struct flock *savelock, int type)
{
    fprintf(stderr, "%s\n", reqP->buf);

    if (check_id(reqP->id) == 0)
    {
        write(reqP->conn_fd, "[Error] Operation failed. Please try again.\n", 44);
        return -1;
    }
    else
    {
        if (type == 0)
        {
            registerRecord r;

            fcntl(fd, F_GETLK, lock); /* Overwrites lock structure with preventors. */

            if (lock->l_type == F_WRLCK)
            {
                // write(reqP->conn_fd, "being modifying\n", 16);
                write(reqP->conn_fd, "Locked.\n", 8);
                return -1;
            }
            else
            {
                if (fcntl(fd, F_SETLK, savelock) == -1)
                {
                    // write(reqP->conn_fd, "set lock fail\n", 14);
                    write(reqP->conn_fd, "Locked.\n", 8);
                    return -1;
                }

                pread(fd, &r, sizeof(registerRecord), pos);

                write(reqP->conn_fd, "Your preference order is ", 25);
                for (int i = 1; i <= 3; i++)
                {
                    if (r.AZ == i)
                        write(reqP->conn_fd, "AZ", 2);

                    else if (r.BNT == i)
                        write(reqP->conn_fd, "BNT", 3);

                    else if (r.Moderna == i)
                        write(reqP->conn_fd, "Moderna", 7);

                    if (i != 3)
                        write(reqP->conn_fd, " > ", 3);
                    else
                        write(reqP->conn_fd, ".\n", 2);
                }
            }
        }
        else if (type == 1)
        {
            registerRecord r;

            fcntl(fd, F_GETLK, lock); /* Overwrites lock structure with preventors. */

            if (lock->l_type == F_WRLCK || lock->l_type == F_RDLCK || write_locked[pos])
            {
                // write(reqP->conn_fd, "being reading or modifying\n", 27);
                write(reqP->conn_fd, "Locked.\n", 8);
                return -1;
            }
            else
            {
                if (fcntl(fd, F_SETLK, savelock) == -1)
                {
                    // write(reqP->conn_fd, "set lock fail\n", 14);
                    write(reqP->conn_fd, "Locked.\n", 8);
                    return -1;
                }

                pread(fd, &r, sizeof(registerRecord), pos);

                write(reqP->conn_fd, "Your preference order is ", 25);
                for (int i = 1; i <= 3; i++)
                {
                    if (r.AZ == i)
                        write(reqP->conn_fd, "AZ", 2);

                    else if (r.BNT == i)
                        write(reqP->conn_fd, "BNT", 3);

                    else if (r.Moderna == i)
                        write(reqP->conn_fd, "Moderna", 7);

                    if (i != 3)
                        write(reqP->conn_fd, " > ", 3);
                    else
                        write(reqP->conn_fd, ".\n", 2);
                }
            }
        }

        return 1;
    }
}

int modify(int fd, request *reqP, int pos)
{
    fprintf(stderr, "%s\n", reqP->buf);

    if (check_order(reqP->buf, reqP->buf_len) == 0)
    {
        write(reqP->conn_fd, "[Error] Operation failed. Please try again.\n", 44);
        return -1;
    }
    else
    {
        registerRecord r;
        r.id = reqP->id;
        r.AZ = reqP->buf[0] - '0';
        r.BNT = reqP->buf[2] - '0';
        r.Moderna = reqP->buf[4] - '0';

        pwrite(fd, &r, sizeof(registerRecord), pos);

        char id[256];
        sprintf(id, "%d", reqP->id);
        write(reqP->conn_fd, "Preference order for ", 21);
        write(reqP->conn_fd, id, 6);
        write(reqP->conn_fd, " modified successed, new preference order is ", 45);
        for (int i = 1; i <= 3; i++)
        {
            if (reqP->buf[0] - '0' == i)
                write(reqP->conn_fd, "AZ", 2);

            else if (reqP->buf[2] - '0' == i)
                write(reqP->conn_fd, "BNT", 3);

            else if (reqP->buf[4] - '0' == i)
                write(reqP->conn_fd, "Moderna", 7);

            if (i != 3)
                write(reqP->conn_fd, " > ", 3);
            else
                write(reqP->conn_fd, ".\n", 2);
        }
        return 1;
    }
}

void init_query_arr(int *a)
{
    for (int i = 0; i <= maxfd; i++)
    {
        a[i] = 0;
    }
}

void change_req_buf_to_id(request *reqP)
{
    if (reqP->buf_len == 6)
        reqP->id = atoi(reqP->buf);
    else
        reqP->id = 0;
}

int main(int argc, char **argv)
{

    // Parse args.
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in cliaddr; // used by accept()
    int clilen;

    int conn_fd; // fd for a new connection with client
    int file_fd; // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Initialize server
    init_server((unsigned short)atoi(argv[1]));

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    // registerRecord
    int fd = open("registerRecord", O_RDWR);

    // fdarray
    fd_set rfd, working_set;
    struct timeval timeout;

    FD_ZERO(&rfd);

    int queried[maxfd + 1];
    init_query_arr(queried);

    struct flock *lockarr;
    lockarr = calloc(maxfd + 1, sizeof(struct flock));

    // poll listen fd
    struct pollfd *sockfd;
    sockfd = calloc(1, sizeof(struct pollfd));
    sockfd[0].fd = svr.listen_fd;
    sockfd[0].events = POLLIN;

    while (1)
    {
        // TODO: Add IO multiplexing

        // Check new connection
        if (poll(sockfd, 1, 50))
        {
            if (sockfd[0].revents != 0 && sockfd[0].revents & POLLIN)
            {
                clilen = sizeof(cliaddr);
                conn_fd = accept(svr.listen_fd, (struct sockaddr *)&cliaddr, (socklen_t *)&clilen);
                if (conn_fd < 0)
                {
                    if (errno == EINTR || errno == EAGAIN)
                    {
                        fprintf(stderr, "EAGAIN\n");
                        // do nothing
                    }
                    else
                    {
                        if (errno == ENFILE)
                        {
                            (void)fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                            continue;
                        }
                        ERR_EXIT("accept");
                    }
                }
                else
                {
                    requestP[conn_fd].conn_fd = conn_fd;
                    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);

                    FD_SET(requestP[conn_fd].conn_fd, &rfd);

                    set_fl(requestP[conn_fd].conn_fd, O_NONBLOCK);

                    write(requestP[conn_fd].conn_fd, "Please enter your id (to check your preference order):\n", 55);
                }
            }
        }

        memcpy(&working_set, &rfd, sizeof(rfd));

        timeout.tv_sec = 0;
        timeout.tv_usec = 50;

        select(maxfd + 1, &working_set, NULL, NULL, &timeout);

// TODO: handle requests from clients
#ifdef READ_SERVER

        for (int i = 0; i < maxfd + 1; i++)
        {
            if (FD_ISSET(i, &working_set))
            {
                int ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf

                change_req_buf_to_id(&requestP[i]);

                fprintf(stderr, "ret = %d ", ret);
                if (ret == 0)
                {
                    fprintf(stderr, "bad request from %s\n", requestP[i].host);
                    FD_CLR(i, &rfd);
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                }
                else
                {
                    int pos = (requestP[i].id - 902001) * 16;

                    struct flock lock, savelock;
                    lock.l_type = F_RDLCK;
                    lock.l_whence = SEEK_SET;
                    lock.l_start = pos;
                    lock.l_len = 16;
                    savelock = lock;

                    if (query(fd, &requestP[i], pos, &lock, &savelock, 0))
                    {
                        savelock.l_type = F_UNLCK;
                        fcntl(fd, F_SETLK, &savelock);
                        FD_CLR(i, &rfd);
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                }
            }
        }

#elif defined WRITE_SERVER

        for (int i = 0; i < maxfd + 1; i++)
        {
            if (FD_ISSET(i, &working_set))
            {

                if (!queried[i])
                {
                    int ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf

                    change_req_buf_to_id(&requestP[i]);

                    fprintf(stderr, "query ret = %d ", ret);
                    if (ret == 0)
                    {
                        fprintf(stderr, "bad request from %s\n", requestP[i].host);
                        FD_CLR(i, &rfd);
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                        continue;
                    }
                    else
                    {
                        int pos = (requestP[i].id - 902001) * 16;

                        struct flock lock, savelock;
                        lock.l_type = F_WRLCK;
                        lock.l_whence = SEEK_SET;
                        lock.l_start = pos;
                        lock.l_len = 16;
                        savelock = lock;

                        int ret = query(fd, &requestP[i], pos, &lock, &savelock, 1);

                        if (ret < 0)
                        {
                            FD_CLR(i, &rfd);
                            close(requestP[i].conn_fd);
                            free_request(&requestP[i]);
                            continue;
                        }
                        else if (ret > 0)
                        {
                            queried[i] = 1;
                            lockarr[i] = savelock;
                            write_locked[pos] = 1;
                            write(requestP[i].conn_fd, "Please input your preference order respectively(AZ,BNT,Moderna):\n", 65);
                        }
                    }
                }
                else
                {
                    int pos = (requestP[i].id - 902001) * 16;

                    int ret = handle_read(&requestP[i]);

                    fprintf(stderr, "modify ret = %d ", ret);
                    if (ret == 0)
                    {
                        fprintf(stderr, "bad request from %s\n", requestP[i].host);
                        queried[i] = 0;
                        FD_CLR(i, &rfd);
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                        continue;
                    }
                    else if (modify(fd, &requestP[i], pos))
                    {
                        queried[i] = 0;
                        write_locked[pos] = 0;
                        lockarr[i].l_type = F_UNLCK;
                        fcntl(fd, F_SETLK, &lockarr[i]);
                        FD_CLR(i, &rfd);
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                }
            }
        }

#endif
    }
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request *reqP)
{
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request *reqP)
{
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

static void init_server(unsigned short port)
{
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0)
        ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&tmp, sizeof(tmp)) < 0)
    {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0)
    {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request *)malloc(sizeof(request) * maxfd);
    if (requestP == NULL)
    {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++)
    {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}