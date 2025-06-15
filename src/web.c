#include <config.h>
#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/kvalloc.h>
#include <tx/net/tcp.h>
#include <tx/print.h>
#include <tx/ramfs.h>
#include <tx/sched.h>
#include <tx/string.h>
#include <tx/time.h>
#include <tx/web.h>

///////////////////////////////////////////////////////////////////////////////
// HTTP request parsing and response creation                                //
///////////////////////////////////////////////////////////////////////////////

enum http_method {
    HTTP_METHOD_GET,
};

enum http_version {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
};

enum http_status {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
};

enum http_content_type {
    HTTP_CONTENT_TYPE_TEXT_HTML,
    HTTP_CONTENT_TYPE_TEXT_PLAIN,
    HTTP_CONTENT_TYPE_TEXT_CSS,
    HTTP_CONTENT_TYPE_IMAGE_PNG,
};

struct http_request {
    enum http_method method;
    struct str path;
    enum http_version version;
    bool valid;
};

struct_result(http_method, enum http_method);
struct_result(http_version, enum http_version);

static struct str http_get_file_extension(struct str path)
{
    struct option_sz dot_pos = str_find_char_reverse(path, '.');
    if (dot_pos.is_none)
        return str_new(NULL, 0);

    sz pos = option_sz_checked(dot_pos);
    return str_new(path.dat + pos, path.len - pos);
}

static enum http_content_type http_get_content_type_from_extension(struct str extension)
{
    if (str_is_equal(extension, STR(".html")) || str_is_equal(extension, STR(".htm"))) {
        return HTTP_CONTENT_TYPE_TEXT_HTML;
    } else if (str_is_equal(extension, STR(".css"))) {
        return HTTP_CONTENT_TYPE_TEXT_CSS;
    } else if (str_is_equal(extension, STR(".png"))) {
        return HTTP_CONTENT_TYPE_IMAGE_PNG;
    } else {
        return HTTP_CONTENT_TYPE_TEXT_PLAIN;
    }
}

static struct result_http_method http_parse_method(struct str method_str)
{
    if (str_is_equal(method_str, STR("GET"))) {
        return result_http_method_ok(HTTP_METHOD_GET);
    }
    return result_http_method_error(EINVAL);
}

static struct result_http_version http_parse_version(struct str version_str)
{
    if (str_is_equal(version_str, STR("HTTP/1.1"))) {
        return result_http_version_ok(HTTP_VERSION_1_1);
    } else if (str_is_equal(version_str, STR("HTTP/1.0"))) {
        return result_http_version_ok(HTTP_VERSION_1_0);
    }
    return result_http_version_error(EINVAL);
}

static struct str http_status_to_string(enum http_status status)
{
    switch (status) {
    case HTTP_STATUS_OK:
        return STR("OK");
    case HTTP_STATUS_BAD_REQUEST:
        return STR("Bad Request");
    case HTTP_STATUS_FORBIDDEN:
        return STR("Forbidden");
    case HTTP_STATUS_NOT_FOUND:
        return STR("Not Found");
    case HTTP_STATUS_METHOD_NOT_ALLOWED:
        return STR("Method Not Allowed");
    default:
        return STR("Unknown");
    }
}

static struct str http_content_type_to_string(enum http_content_type content_type)
{
    switch (content_type) {
    case HTTP_CONTENT_TYPE_TEXT_HTML:
        return STR("text/html");
    case HTTP_CONTENT_TYPE_TEXT_PLAIN:
        return STR("text/plain");
    case HTTP_CONTENT_TYPE_TEXT_CSS:
        return STR("text/css");
    case HTTP_CONTENT_TYPE_IMAGE_PNG:
        return STR("image/png");
    default:
        crash("Invalid content type");
    }
}

static struct http_request http_parse_request(struct str request_data)
{
    struct http_request req = { 0 };

    struct option_sz first_space = str_find_char(request_data, ' ');
    if (first_space.is_none) {
        req.valid = false;
        return req;
    }

    sz space1_pos = option_sz_checked(first_space);
    struct str method_str = str_new(request_data.dat, space1_pos);
    struct result_http_method method_result = http_parse_method(method_str);
    if (method_result.is_error) {
        req.valid = false;
        return req;
    }
    req.method = result_http_method_checked(method_result);

    struct str remaining = str_new(request_data.dat + space1_pos + 1, request_data.len - space1_pos - 1);

    struct option_sz second_space = str_find_char(remaining, ' ');
    if (second_space.is_none) {
        req.valid = false;
        return req;
    }

    sz space2_pos = option_sz_checked(second_space);
    req.path = str_new(remaining.dat, space2_pos);

    struct str version_and_rest = str_new(remaining.dat + space2_pos + 1, remaining.len - space2_pos - 1);

    struct option_sz newline_pos = str_find_char(version_and_rest, '\r');
    if (newline_pos.is_none) {
        newline_pos = str_find_char(version_and_rest, '\n');
        if (newline_pos.is_none) {
            req.valid = false;
            return req;
        }
    }

    sz nl_pos = option_sz_checked(newline_pos);
    struct str version_str = str_new(version_and_rest.dat, nl_pos);
    struct result_http_version version_result = http_parse_version(version_str);
    if (version_result.is_error) {
        req.valid = false;
        return req;
    }
    req.version = result_http_version_checked(version_result);

    req.valid = true;
    return req;
}

static struct result http_build_header(enum http_status status, enum http_content_type content_type, sz body_len,
                                       struct byte_buf *response_buf)
{
    assert(response_buf);

    // We need to convert the byte buffer to a string buffer to format the header. But changes to the state of the
    // string buffer don't affect the `response_buf`. So before returning from this function on successful completion,
    // we need to update `response_buf`. As a side effect, `response_buf` is not updated if the header was only built
    // partially. This is neat.
    struct str_buf buf = str_buf_from_byte_buf(*response_buf);

    struct result res = fmt(&buf, STR("HTTP/1.1 %u %s\r\n"), (u32)status, http_status_to_string(status));
    if (res.is_error)
        return res;

    res = fmt(&buf, STR("Content-Type: %s\r\n"), http_content_type_to_string(content_type));
    if (res.is_error)
        return res;

    assert(body_len >= 0);
    res = fmt(&buf, STR("Content-Length: %lu\r\n"), (u64)body_len);
    if (res.is_error)
        return res;

    res = str_buf_append(&buf, STR("Connection: close\r\n"));
    if (res.is_error)
        return res;

    res = str_buf_append(&buf, STR("\r\n"));
    if (res.is_error)
        return res;

    *response_buf = byte_buf_from_str_buf(buf);

    return result_ok();
}

static struct result http_build_response(enum http_status status, enum http_content_type content_type,
                                         struct byte_view body, struct byte_buf *response_buf)
{
    assert(response_buf);

    // If the buffer can't fit the body alone (without a header), we don't even need to bother building the header.
    if (response_buf->cap < response_buf->len + body.len)
        return result_error(ENOMEM);

    struct result res = http_build_header(status, content_type, body.len, response_buf);
    if (res.is_error)
        return res;

    sz n_appended = byte_buf_append(response_buf, body);
    if (n_appended != body.len)
        return result_error(ENOMEM);

    return result_ok();
}

#define HTML_PAGE(title, content)                                                                   \
    "<!DOCTYPE html>"                                                                               \
    "<html lang=\"en\"><head>"                                                                      \
    "<meta charset=\"UTF-8\">"                                                                      \
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"                    \
    "<title>" title "</title>"                                                                      \
    "</head><body>" content "<footer><hr/><small>Served by Tatix (" GIT_COMMIT ")</small></footer>" \
    "</body></html>"

static struct str forbidden_body =
    STR_STATIC(HTML_PAGE("403 Forbidden", "<h1>403 Forbidden</h1><p>Directory listing not allowed.</p>"));
static struct str not_found_body =
    STR_STATIC(HTML_PAGE("404 Not Found", "<h1>404 Not Found</h1><p>The requested file was not found.</p>"));

static struct result http_serve_file(struct ram_fs_node *root, struct str path, struct byte_buf *response_buf)
{
    assert(root);
    assert(response_buf);

    if (path.len == 0 || (path.len == 1 && path.dat[0] == '/')) {
        path = STR("/index.html");
    }

    struct result_ram_fs_node file_result = ram_fs_open(root, path);
    if (file_result.is_error) {
        return http_build_response(HTTP_STATUS_NOT_FOUND, HTTP_CONTENT_TYPE_TEXT_HTML,
                                   byte_view_from_str(not_found_body), response_buf);
    }

    struct ram_fs_node *file_node = result_ram_fs_node_checked(file_result);
    if (file_node->type != RAM_FS_TYPE_FILE) {
        return http_build_response(HTTP_STATUS_FORBIDDEN, HTTP_CONTENT_TYPE_TEXT_HTML,
                                   byte_view_from_str(forbidden_body), response_buf);
    }

    struct str extension = http_get_file_extension(path);
    enum http_content_type content_type = http_get_content_type_from_extension(extension);

    return http_build_response(HTTP_STATUS_OK, content_type, byte_view_from_buf(file_node->data), response_buf);
}

static struct result http_handle_request(struct ram_fs_node *root, struct str request_data,
                                         struct byte_buf *response_buf)
{
    assert(root);
    assert(response_buf);

    struct http_request req = http_parse_request(request_data);

    if (!req.valid) {
        static struct str bad_request_body =
            STR_STATIC("<html><body><h1>400 Bad Request</h1><p>Invalid HTTP request.</p></body></html>");

        return http_build_response(HTTP_STATUS_BAD_REQUEST, HTTP_CONTENT_TYPE_TEXT_HTML,
                                   byte_view_from_str(bad_request_body), response_buf);
    }

    if (req.method != HTTP_METHOD_GET) {
        static struct str method_not_allowed_body = STR_STATIC(
            "<html><body><h1>405 Method Not Allowed</h1><p>Only GET requests are supported.</p></body></html>");

        return http_build_response(HTTP_STATUS_METHOD_NOT_ALLOWED, HTTP_CONTENT_TYPE_TEXT_HTML,
                                   byte_view_from_str(method_not_allowed_body), response_buf);
    }

    return http_serve_file(root, req.path, response_buf);
}

///////////////////////////////////////////////////////////////////////////////
// Connection handling                                                       //
///////////////////////////////////////////////////////////////////////////////

#define WEB_NUM_RECV_RETRIES 10

static struct tcp_conn *web_wait_accept_conn(struct tcp_conn *listen_conn)
{
    struct tcp_conn *conn = NULL;
    while (!(conn = tcp_conn_accept(listen_conn)))
        sleep_ms(time_ms_new(10));
    return conn;
}

static struct result web_respond_close(struct tcp_conn *conn, struct byte_view response, struct send_buf sb,
                                       struct arena tmp)
{
    struct result_sz res = tcp_conn_send(conn, response, sb, tmp);
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

static struct result web_handle_conn(struct tcp_conn *listen_conn, struct ram_fs_node *root, struct send_buf sb,
                                     struct arena tmp)
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

    struct byte_buf response_buf = byte_buf_from_array(byte_array_from_arena(0x6000, &tmp));

    struct result http_res = http_handle_request(root, str_from_byte_buf(recv_buf), &response_buf);
    if (http_res.is_error) {
        tcp_conn_close(&conn, sb, tmp);
        return result_error(http_res.code);
    }

    struct str response_start = str_new((char *)response_buf.dat, MIN(response_buf.len, 200));
    print_dbg(PDBG, STR("Transmitting response:\n%s ...\n"), response_start);

    return web_respond_close(conn, byte_view_from_buf(response_buf), sb, tmp);
}

struct result web_listen(struct ipv4_addr ip_addr, u16 port, struct ram_fs_node *root)
{
    struct arena tmp = arena_new(option_byte_array_checked(kvalloc_alloc(0x8000, 64)));
    struct send_buf sb = send_buf_new(arena_new(option_byte_array_checked(kvalloc_alloc(0x8000, 64))));

    struct tcp_conn *listen_conn = tcp_conn_listen(ip_addr, port, tmp);

    print_dbg(PINFO, STR("Listening for connections on %s:%hu\n"), ipv4_addr_format(ip_addr, &tmp), port);

    while (true) {
        // TODO: Fix sending big files!
        struct result res = web_handle_conn(listen_conn, root, sb, tmp);
        if (res.is_error) {
            print_dbg(PDBG, STR("Error handling connection: %s\n"), error_code_str(res.code));
            return res;
        }
        sleep_ms(time_ms_new(10));
    }
}
