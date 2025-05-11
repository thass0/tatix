#include <tx/net/ip_addr.h>

static inline bool in_char_range(char x, char beg, char end)
{
    return beg <= x && x <= end;
}

static inline u8 char_to_digit(char x)
{
    assert(in_char_range(x, '0', '9'));
    return x - '0';
}

static struct ipv4_addr prefix_length_to_mask(sz prefix_length)
{
    assert(1 <= prefix_length && prefix_length <= 32);

    u32 raw = 0xffffffff << (32 - prefix_length);

    struct ipv4_addr mask;
    mask.addr[0] = (raw >> 24) & 0xff;
    mask.addr[1] = (raw >> 16) & 0xff;
    mask.addr[2] = (raw >> 8) & 0xff;
    mask.addr[3] = raw & 0xff;

    return mask;
}

struct result_ipv4_addr_parsed ipv4_addr_parse(struct str str)
{
    struct ipv4_addr_parsed pa;
    pa.addr = ipv4_addr_new(0, 0, 0, 0);
    pa.mask = ipv4_addr_new(0, 0, 0, 0);

    sz i = 0;
    sz addr_idx = 0;

    while (i < str.len) {
        do {
            if (i + 2 < str.len && str.dat[i] == '2' && str.dat[i + 1] == '5' &&
                in_char_range(str.dat[i + 2], '0', '5')) {
                pa.addr.addr[addr_idx++] = 250 + char_to_digit(str.dat[i + 2]);
                i += 3;
                break;
            }

            if (i + 2 < str.len && str.dat[i] == '2' && in_char_range(str.dat[i + 1], '0', '4') &&
                in_char_range(str.dat[i + 2], '0', '9')) {
                pa.addr.addr[addr_idx++] = 200 + (10 * char_to_digit(str.dat[i + 1])) + char_to_digit(str.dat[i + 2]);
                i += 3;
                break;
            }

            if (i + 2 < str.len && str.dat[i] == '1' && in_char_range(str.dat[i + 1], '0', '9') &&
                in_char_range(str.dat[i + 2], '0', '9')) {
                pa.addr.addr[addr_idx++] = 100 + (10 * char_to_digit(str.dat[i + 1])) + char_to_digit(str.dat[i + 2]);
                i += 3;
                break;
            }

            if (i + 1 < str.len && in_char_range(str.dat[i], '1', '9') && in_char_range(str.dat[i + 1], '0', '9')) {
                pa.addr.addr[addr_idx++] = (10 * char_to_digit(str.dat[i])) + char_to_digit(str.dat[i + 1]);
                i += 2;
                break;
            }

            if (in_char_range(str.dat[i], '0', '9')) {
                pa.addr.addr[addr_idx++] = char_to_digit(str.dat[i]);
                i++;
                break;
            }

            // We fell though so we couldn't parse any valid octet.
            goto error;
        } while (0);

        if (addr_idx >= 4 || i >= str.len)
            break; // We are done

        if (str.dat[i] != '.')
            goto error;

        i++;
    }

    if (addr_idx != 4)
        goto error; // We need to have parsed four components.

    sz prefix_length = 32; // By default, all bits of the IPv4 address count.

    do {
        if (i < str.len) {
            if (str.dat[i] != '/')
                goto error;

            i++;

            // Any number from 1-32. Nothing means the mask is all ones (like /32).
            if (i + 1 < str.len && str.dat[i] == '3' && in_char_range(str.dat[i + 1], '0', '2')) {
                prefix_length = 30 + char_to_digit(str.dat[i + 1]);
                i += 2;
                break;
            }

            if (i + 1 < str.len && in_char_range(str.dat[i], '1', '2') && in_char_range(str.dat[i + 1], '0', '9')) {
                prefix_length = (10 * char_to_digit(str.dat[i])) + char_to_digit(str.dat[i + 1]);
                i += 2;
                break;
            }

            if (i < str.len && in_char_range(str.dat[i], '1', '9')) {
                prefix_length = char_to_digit(str.dat[i]);
                i++;
                break;
            }

            goto error;
        }
    } while (0);

    if (i != str.len)
        goto error; // Must have parsed the entire string by now.

    pa.mask = prefix_length_to_mask(prefix_length);

    return result_ipv4_addr_parsed_ok(pa);

error:
    return result_ipv4_addr_parsed_error(EINVAL);
}

void ipv4_test_addr_parse(struct arena arn)
{
    struct result_ipv4_addr_parsed pa_res;
    struct ipv4_addr_parsed pa;

    // Valid addresses
    pa_res = ipv4_addr_parse(STR("0.0.0.0"));
    assert(!pa_res.is_error);
    pa = result_ipv4_addr_parsed_checked(pa_res);
    assert(pa.addr.addr[0] == 0 && pa.addr.addr[1] == 0 && pa.addr.addr[2] == 0 && pa.addr.addr[3] == 0);
    assert(pa.mask.addr[0] == 0xff && pa.mask.addr[1] == 0xff && pa.mask.addr[2] == 0xff && pa.mask.addr[3] == 0xff);

    pa_res = ipv4_addr_parse(STR("255.255.255.255"));
    assert(!pa_res.is_error);
    pa = result_ipv4_addr_parsed_checked(pa_res);
    assert(pa.addr.addr[0] == 255 && pa.addr.addr[1] == 255 && pa.addr.addr[2] == 255 && pa.addr.addr[3] == 255);
    assert(pa.mask.addr[0] == 0xff && pa.mask.addr[1] == 0xff && pa.mask.addr[2] == 0xff && pa.mask.addr[3] == 0xff);

    pa_res = ipv4_addr_parse(STR("1.23.195.7"));
    assert(!pa_res.is_error);
    pa = result_ipv4_addr_parsed_checked(pa_res);
    assert(pa.addr.addr[0] == 1 && pa.addr.addr[1] == 23 && pa.addr.addr[2] == 195 && pa.addr.addr[3] == 7);
    assert(pa.mask.addr[0] == 0xff && pa.mask.addr[1] == 0xff && pa.mask.addr[2] == 0xff && pa.mask.addr[3] == 0xff);

    pa_res = ipv4_addr_parse(STR("127.42.8.100"));
    assert(!pa_res.is_error);
    pa = result_ipv4_addr_parsed_checked(pa_res);
    assert(pa.addr.addr[0] == 127 && pa.addr.addr[1] == 42 && pa.addr.addr[2] == 8 && pa.addr.addr[3] == 100);
    assert(pa.mask.addr[0] == 0xff && pa.mask.addr[1] == 0xff && pa.mask.addr[2] == 0xff && pa.mask.addr[3] == 0xff);

    // Invalid addresses
    pa_res = ipv4_addr_parse(STR("256.0.0.0"));
    assert(pa_res.is_error);

    pa_res = ipv4_addr_parse(STR("192.168.1"));
    assert(pa_res.is_error);

    pa_res = ipv4_addr_parse(STR("001.002.003.004"));
    assert(pa_res.is_error);

    // CIDR tests
    struct str_buf sbuf = str_buf_from_byte_array(byte_array_from_arena(32, &arn));
    for (sz prefix = 1; prefix <= 32; prefix++) {
        sbuf.len = 0;
        str_buf_append(&sbuf, STR("192.168.0.1/")); // Address chosen at random.
        fmt_append_i64(prefix, &sbuf);

        pa_res = ipv4_addr_parse(str_from_buf(sbuf));
        assert(!pa_res.is_error);
        pa = result_ipv4_addr_parsed_checked(pa_res);
        assert(pa.addr.addr[0] == 192 && pa.addr.addr[1] == 168 && pa.addr.addr[2] == 0 && pa.addr.addr[3] == 1);

        u32 mask_raw = 0xffffffff << (32 - prefix);
        assert(pa.mask.addr[0] == ((mask_raw >> 24) & 0xff));
        assert(pa.mask.addr[1] == ((mask_raw >> 16) & 0xff));
        assert(pa.mask.addr[2] == ((mask_raw >> 8) & 0xff));
        assert(pa.mask.addr[3] == (mask_raw & 0xff));
    }

    print_dbg(PINFO, STR("IPv4 address parse tests passed\n"));
}
