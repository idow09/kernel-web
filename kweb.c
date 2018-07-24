#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/stat.h>
#include <net/sock.h>


#define BUFFSIZE (1*1024)

static unsigned short port = 8000;
module_param(port, ushort, S_IRUGO);
static unsigned int KiB = 2*1024;
module_param(KiB, uint, S_IRUGO);

#ifdef KWEB_DEBUG
    #define KWEBMSG(_msg,args...) printk( KERN_DEBUG "kweb: " _msg, ## args)
#else
    #define KWEBMSG(_msg,args...) /* print nothing */
#endif

int start = 0;

static void connection_handler(struct work_struct * ignore);
void http_server(struct socket *csocket);
char *inet_ntoa(struct in_addr in);
int sendmsg(struct socket *csocket, const void *data, size_t datalength, int flags);

struct socket *sock;

static DECLARE_WORK(connection_work, connection_handler);

static void connection_handler(struct work_struct * ignore)
{
    int rc,s_status,len = 0;
    struct socket *newsock;
    struct sockaddr_in locaddr;
    struct sockaddr_in peeraddr;
    char * client_ip;

    KWEBMSG("Socket server work now.\n");
    rc = sock_create(PF_INET, SOCK_STREAM, 0, &sock);
    
    if ( rc < 0 ) {
        KWEBMSG( "ERROR - Could not create socket\n");
        return;
    }


    memset(&locaddr, 0, sizeof(locaddr));
    locaddr.sin_family = AF_INET;  
    locaddr.sin_port = htons(port);  
    locaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    rc = kernel_bind(sock, (struct sockaddr *)&locaddr, sizeof(locaddr));

    if (rc == -EADDRINUSE) {
        KWEBMSG( "ERROR - Port %d already in use\n",port);
        return;
    }
    if (rc != 0) {
        KWEBMSG( "ERROR - Can't bind to port %d\n",port);
        return;
    }

    rc = kernel_listen(sock, 0);

    KWEBMSG("HTTP server listening on port:%d\n",port);
    KWEBMSG("Waiting for client's request\n");


    do {

        rc = wait_event_interruptible_timeout(
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
                *(sock->sk->sk_sleep),
#else
                *sk_sleep(sock->sk),
#endif
                (s_status = kernel_accept(sock, &newsock, O_NONBLOCK)) >= 0,
                5 * HZ 
            );
        /* Always relaunch after 5 sec, so it wouldn't block 'kweb_module_cleanup'*/

        if (rc == 0) {
            KWEBMSG("Time Out\n");
            continue;
        }

        KWEBMSG("%d,%d\n",rc,s_status);

        rc = kernel_getpeername(newsock, (struct sockaddr *)&peeraddr, &len);
        client_ip = inet_ntoa(peeraddr.sin_addr);
        KWEBMSG("Client IP:%s, Client port:%d\n", client_ip, peeraddr.sin_port);
        kfree(client_ip);

        http_server(newsock);

        KWEBMSG("Close client socket\n");
        if( newsock != NULL ) sock_release(newsock);
        newsock = NULL;

    } while (start);

}

void http_server(struct socket *csocket)
{
    char *request;

    int length;
    struct msghdr msg;
    struct kvec iov;

    request = kmalloc(BUFFSIZE, GFP_KERNEL);
    memset(request, 0, BUFFSIZE);

    iov.iov_base = (void *)request;
    iov.iov_len = (__kernel_size_t)BUFFSIZE;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    length = kernel_recvmsg(
        csocket, /*Client socket*/
        &msg, /*Received message*/
        &iov, /*Input s/g array for message data(msg.msg_iov)*/
        1, /*Size of input s/g array(msg.msg_iovlen)*/
        BUFFSIZE, /*Number of bytes to read*/
        0 /*Message flags*/ 
    );

    if ( length <= 0 ) {
        KWEBMSG( "Read from socket failed\n");
    }
    else {
        KWEBMSG("Request:%s\n",request);

        KWEBMSG("TCP request received\n");

        sendmsg(csocket,request,strlen(request),0);

        KWEBMSG("TCP sent msg\n");
    }

    kfree(request);
}

int sendmsg(struct socket *csocket, const void *data, size_t datalength, int flags)
{
    struct msghdr msg;
    struct kvec iov;
    int len;

    iov.iov_base = (void *)data;
    iov.iov_len = (__kernel_size_t)datalength;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    msg.msg_flags = flags;

    len = kernel_sendmsg(csocket, &msg, &iov, 1, datalength);
    
    return len;
}

static int __init kweb_module_init(void)
{
    int rv;
    start = 1;

    rv = schedule_work(&connection_work);

    KWEBMSG("schedule_work: %d\n",rv);
    KWEBMSG("Kweb module init\n");

    return 0;
}

static void __exit kweb_module_cleanup(void)
{
    int rv;
    start = 0;

    rv = cancel_work_sync(&connection_work);
    KWEBMSG("cancel_work_sync: %d\n",rv);

    rv = flush_work(&connection_work);
    KWEBMSG("flush_work: %d\n",rv);

    
    if( sock != NULL ) sock_release(sock);
    sock = NULL;


    KWEBMSG("Kweb module exit\n");
}

module_init(kweb_module_init);
module_exit(kweb_module_cleanup);
MODULE_DESCRIPTION("Kernel HTTP server for speed test.");
MODULE_LICENSE("GPL");

char *inet_ntoa(struct in_addr in)
{
    char* str_ip = NULL;
    u_int32_t int_ip = 0;
    
    str_ip = kmalloc(16 * sizeof(char), GFP_KERNEL);
    if (!str_ip)
        return NULL;
    else
        memset(str_ip, 0, 16);

    int_ip = in.s_addr;
    
    sprintf(str_ip, "%d.%d.%d.%d",  (int_ip      ) & 0xFF,
                                    (int_ip >> 8 ) & 0xFF,
                                    (int_ip >> 16) & 0xFF,
                                    (int_ip >> 24) & 0xFF);
    return str_ip;
}
