#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/net/tcp.h>
#include <tx/print.h>
#include <tx/sched.h>
#include <tx/time.h>
#include <tx/web.h>

#define WEB_NUM_RECV_RETRIES 10

static struct str web_default_response = STR_STATIC("HTTP/1.1 200 OK\r\n"
                                                    "Content-Type: text/html\r\n"
                                                    "Content-Length: 49\r\n"
                                                    "Connection: close\r\n"
                                                    "\r\n"
                                                    "<html><body><h1>Hello, World!</h1></body></html>\n");

static struct tcp_conn *web_wait_accept_conn(struct tcp_conn *listen_conn)
{
    struct tcp_conn *conn = NULL;
    while (!(conn = tcp_conn_accept(listen_conn)))
        sleep_ms(time_ms_new(10));
    return conn;
}

static struct result web_respond_close(struct tcp_conn *conn, struct str response, struct send_buf sb, struct arena tmp)
{
    struct result_sz res = tcp_conn_send(conn, byte_view_from_str(response), sb, tmp);
    if (res.is_error)
        return result_error(res.code);

    if (result_sz_checked(res) != response.len)
        return result_error(EIO);

    return tcp_conn_close(&conn, sb, tmp);
}

static struct result_sz web_recv_retry(struct tcp_conn *conn, struct byte_buf *recv_buf)
{
    assert(conn);
    assert(recv_buf);

    sz n_received = 0;

    for (sz i = 0; i < WEB_NUM_RECV_RETRIES; i++) {
        struct result_sz res = tcp_conn_recv(conn, recv_buf);
        if (res.is_error)
            return result_sz_error(res.code);

        n_received = result_sz_checked(res);
        if (n_received)
            break;

        sleep_ms(time_ms_new(10));
    }

    return result_sz_ok(n_received);
}

static struct result web_handle_conn(struct tcp_conn *listen_conn, struct ram_fs_node *root __unused,
                                     struct send_buf sb, struct arena tmp)
{
    struct tcp_conn *conn = web_wait_accept_conn(listen_conn);

    struct byte_buf recv_buf = byte_buf_from_array(byte_array_from_arena(1024, &tmp));

    struct result_sz res = web_recv_retry(conn, &recv_buf);
    if (res.is_error)
        return result_error(res.code);

    if (result_sz_checked(res) > recv_buf.cap) {
        // There is more data to be received than what we have space for.
        // TODO: Send HTTP error.
        tcp_conn_close(&conn, sb, tmp);
        return result_ok();
    }

    print_dbg(PINFO, STR("Web server received:\n%s\n"), str_from_byte_buf(recv_buf));

    // TODO: Parse the content of the request and respond with data from the file system :^)

    return web_respond_close(conn, web_default_response, sb, tmp);
}

struct result web_listen(struct ipv4_addr ip_addr, u16 port, struct ram_fs_node *root)
{
    struct arena tmp = arena_new(option_byte_array_checked(kvalloc_alloc(0x4000, 64)));
    struct send_buf sb = send_buf_new(arena_new(option_byte_array_checked(kvalloc_alloc(0x4000, 64))));

    struct tcp_conn *listen_conn = tcp_conn_listen(ip_addr, port, tmp);

    print_dbg(PINFO, STR("Listening for connections on %s:%hu\n"), ipv4_addr_format(ip_addr, &tmp), port);

    while (true) {
        struct result res = web_handle_conn(listen_conn, root, sb, tmp);
        if (res.is_error)
            return res;
        sleep_ms(time_ms_new(10));
    }
}
