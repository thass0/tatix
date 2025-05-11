#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/rtcfg.h>

static struct result_ipv4_addr_parsed rtcfg_parse_option_ip_addr(struct str *str)
{
    if (!str_consume_prefix(str, STR("=")))
        return result_ipv4_addr_parsed_error(EINVAL);

    struct option_sz substr_len_opt = str_find_char(*str, '\n');
    if (substr_len_opt.is_none)
        return result_ipv4_addr_parsed_error(EINVAL);
    sz substr_len = option_sz_checked(substr_len_opt);
    assert(substr_len <= str->len);

    struct result_ipv4_addr_parsed pa_res = ipv4_addr_parse(str_new(str->dat, substr_len));
    if (pa_res.is_error)
        return pa_res;

    str->dat += substr_len;
    str->len -= substr_len;

    return pa_res;
}

static struct result rtcfg_parse(struct runtime_config *rtcfg, struct byte_view raw)
{
    struct str str = str_from_byte_view(raw);

    while (str.len) {
        // Consume empty lines and lines that contain comments.
        if (str_consume_prefix(&str, STR("\n")))
            continue;

        // Comsume lines that just contain comments.
        if (str_consume_prefix(&str, STR("#"))) {
            struct option_sz idx_opt = str_find_char(str, '\n');
            if (idx_opt.is_none)
                return result_error(EINVAL);
            sz len = option_sz_checked(idx_opt) + 1;
            str.dat += len;
            str.len -= len;
            continue;
        }

        if (str_consume_prefix(&str, STR("host_ip"))) {
            struct result_ipv4_addr_parsed res = rtcfg_parse_option_ip_addr(&str);
            if (res.is_error)
                return result_error(res.code);
            rtcfg->host_ip = option_ipv4_addr_ok(result_ipv4_addr_parsed_checked(res).addr);
            continue;
        }

        if (str_consume_prefix(&str, STR("local_ip"))) {
            struct result_ipv4_addr_parsed res = rtcfg_parse_option_ip_addr(&str);
            if (res.is_error)
                return result_error(res.code);
            struct ipv4_addr_parsed pa = result_ipv4_addr_parsed_checked(res);
            rtcfg->local_ip = option_ipv4_addr_ok(pa.addr);
            rtcfg->local_ip_mask = option_ipv4_addr_ok(pa.mask);
            continue;
        }

        if (str_consume_prefix(&str, STR("default_gateway_ip"))) {
            struct result_ipv4_addr_parsed res = rtcfg_parse_option_ip_addr(&str);
            if (res.is_error)
                return result_error(res.code);
            rtcfg->default_gateway_ip = option_ipv4_addr_ok(result_ipv4_addr_parsed_checked(res).addr);
            continue;
        }

        return result_error(EINVAL);
    }

    return result_ok();
}

struct result_runtime_config rtcfg_read_config(struct ram_fs *rfs, struct str cfg_filename, struct arena arn)
{
    assert(rfs);

    struct result_ram_fs_node cfg_file_res = ram_fs_open(rfs, cfg_filename);
    if (cfg_file_res.is_error)
        return result_runtime_config_error(cfg_file_res.code);
    struct ram_fs_node *cfg_file = result_ram_fs_node_checked(cfg_file_res);

    // The config file must have a reasonable size.
    if (cfg_file->data.len > 4096)
        return result_runtime_config_error(ENOMEM);

    struct byte_buf read_buf = byte_buf_from_array(byte_array_from_arena(cfg_file->data.len, &arn));
    struct result_sz n_read_res = ram_fs_read(cfg_file, &read_buf, 0);
    if (n_read_res.is_error)
        return result_runtime_config_error(n_read_res.code);

    // We allocated the buffer to fit the entire file so this must hold.
    assert(result_sz_checked(n_read_res) == cfg_file->data.len);

    struct option_byte_array rtcfg_mem_opt =
        kvalloc_alloc(sizeof(struct runtime_config), alignof(struct runtime_config));
    if (rtcfg_mem_opt.is_none)
        return result_runtime_config_error(ENOMEM);

    struct runtime_config *rtcfg = byte_array_ptr(option_byte_array_checked(rtcfg_mem_opt));
    rtcfg->host_ip = option_ipv4_addr_none();
    rtcfg->local_ip = option_ipv4_addr_none();
    rtcfg->local_ip_mask = option_ipv4_addr_none();
    rtcfg->default_gateway_ip = option_ipv4_addr_none();

    struct result parse_res = rtcfg_parse(rtcfg, byte_view_from_buf(read_buf));
    if (parse_res.is_error)
        return result_runtime_config_error(parse_res.code);

    return result_runtime_config_ok(rtcfg);
}
