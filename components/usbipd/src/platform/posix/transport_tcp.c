/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

/*****************************************************************************
 * TCP Transport Implementation
 *
 * TCP-based transport layer implementation
 *****************************************************************************/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "hal/usbip_osal.h"
#include "hal/usbip_transport.h"

void transport_register(const char* name, struct usbip_transport* trans);

/*****************************************************************************
 * TCP Transport Private Data
 *****************************************************************************/

struct tcp_transport_priv
{
    int fd;        /* Listen socket */
    uint16_t port; /* Listen port */
};

struct tcp_conn_priv
{
    int fd; /* Connection socket */
};

/*****************************************************************************
 * TCP Transport Implementation
 *****************************************************************************/

static int tcp_listen(struct usbip_transport* trans, uint16_t port)
{
    struct tcp_transport_priv* priv = trans->priv;

    int opt = 1;
    struct sockaddr_in addr;

    priv->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (priv->fd < 0)
    {
        perror("socket");
        return -1;
    }

    /* Set SO_REUSEADDR */
    if (setsockopt(priv->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt SO_REUSEADDR");
        close(priv->fd);
        priv->fd = -1;
        return -1;
    }

    /* Bind address */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(priv->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(priv->fd);
        priv->fd = -1;
        return -1;
    }

    /* Start listening */
    if (listen(priv->fd, 10) < 0)
    {
        perror("listen");
        close(priv->fd);
        priv->fd = -1;
        return -1;
    }

    priv->port = port;
    return 0;
}

static struct usbip_conn_ctx* tcp_accept(struct usbip_transport* trans)
{
    struct tcp_transport_priv* priv = trans->priv;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct usbip_conn_ctx* ctx;
    struct tcp_conn_priv* conn_priv;
    int fd;
    int opt = 1;

    fd = accept(priv->fd, (struct sockaddr*)&client_addr, &addr_len);
    if (fd < 0)
    {
        perror("accept");
        return NULL;
    }

    /* Set TCP_NODELAY - Disable Nagle algorithm to reduce latency */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    /* Set SO_KEEPALIVE - Keep connection alive */
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

#ifdef DEBUG
    printf("Accepted connection from %s:%d\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));
#endif

    /* Create connection context */
    ctx = osal_malloc(sizeof(*ctx));
    if (!ctx)
    {
        close(fd);
        return NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
    conn_priv = osal_malloc(sizeof(*conn_priv));
    if (!conn_priv)
    {
        osal_free(ctx);
        close(fd);
        return NULL;
    }

    memset(conn_priv, 0, sizeof(*conn_priv));
    conn_priv->fd = fd;
    ctx->priv = conn_priv;

    return ctx;
}

static ssize_t tcp_recv(struct usbip_conn_ctx* ctx, void* buf, size_t len)
{
    struct tcp_conn_priv* priv = ctx->priv;

    ssize_t n;
    size_t total = 0;

    /* Use MSG_WAITALL to ensure complete data reception */
    while (total < len)
    {
        n = recv(priv->fd, (char*)buf + total, len - total, MSG_WAITALL);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n < 0 && errno == EBADF)
            {
                /* Socket closed by another thread */
                return -1;
            }
            if (n == 0)
            {
                /* Connection closed */
                return 0;
            }
            perror("recv");
            return -1;
        }
        total += n;
    }

    return total;
}

static ssize_t tcp_send(struct usbip_conn_ctx* ctx, const void* buf, size_t len)
{
    struct tcp_conn_priv* priv = ctx->priv;

    ssize_t n;
    size_t total = 0;

    while (total < len)
    {
        n = send(priv->fd, (const char*)buf + total, len - total, MSG_NOSIGNAL);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n < 0)
            {
                perror("send");
            }
            return -1;
        }
        total += n;
    }

    return total;
}

static void tcp_close(struct usbip_conn_ctx* ctx)
{
    int fd;
    struct tcp_conn_priv* priv = NULL;

    if (ctx)
    {
        priv = ctx->priv;
        if (priv)
        {
            if (priv->fd >= 0)
            {
                fd = priv->fd;
                /* Mark as closed first */
                priv->fd = -1;
                /* Force recv() to return immediately */
                shutdown(fd, SHUT_RDWR); 
                close(fd);
            }

            osal_free(priv);
        }

        osal_free(ctx);
    }
}

static void tcp_destroy(struct usbip_transport* trans)
{
    struct tcp_transport_priv* priv = trans->priv;

    if (priv)
    {
        if (priv->fd >= 0)
        {
            close(priv->fd);
            priv->fd = -1;
        }
    }
}

static void tcp_stop(struct usbip_transport* trans)
{
    struct tcp_transport_priv* priv = trans->priv;

    if (priv && priv->fd >= 0)
    {
        /* Close listen socket to interrupt blocking accept() */
        close(priv->fd);
        priv->fd = -1;
    }
}

/*****************************************************************************
 * Create Function (internal)
 *****************************************************************************/
static struct tcp_transport_priv priv = {.fd = -1, .port = 0};
static struct usbip_transport trans = {.priv = &priv,
                                       .listen = tcp_listen,
                                       .accept = tcp_accept,
                                       .recv = tcp_recv,
                                       .send = tcp_send,
                                       .close = tcp_close,
                                       .stop = tcp_stop,
                                       .destroy = tcp_destroy};

__attribute__((section(".usbip.init"), used)) void default_transport_register(void)
{
    transport_register("tcp", &trans);
}