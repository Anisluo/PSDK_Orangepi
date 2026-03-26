/*
 * ffs_init.c — 初始化 FunctionFS ep0 (USB Bulk 描述符)
 *
 * 在 setup_gadget.sh 挂载 FunctionFS 之后、绑定 UDC 之前运行。
 * 写入 USB 描述符到 ep0，使 FunctionFS 进入 ACTIVE 状态，
 * 此后 UDC 才能成功绑定，psdkd 才能打开 ep1/ep2。
 *
 * 用法:
 *   sudo ./ffs_init /dev/usb-ffs/bulk1 [if_num ep_in ep_out] &
 *   echo musb-hdrc.5.auto > /sys/kernel/config/usb_gadget/psdk/UDC
 *   sudo ./psdkd --debug
 *
 * 编译:
 *   gcc -o ffs_init tools/ffs_init.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <linux/usb/functionfs.h>
#include <endian.h>

/* ── USB 描述符 ─────────────────────────────────────────────────────────── */

/* aarch64 是小端，htole 不是编译期常量，直接用原值 */
#define LE16(x) (uint16_t)(x)
#define LE32(x) (uint32_t)(x)

/* FunctionFS 描述符头 (legacy v1) */
struct ffs_desc_v1_head {
    uint32_t magic;     /* FUNCTIONFS_DESCRIPTORS_MAGIC = 1 */
    uint32_t length;
    uint32_t fs_count;
    uint32_t hs_count;
} __attribute__((packed));

/* USB 接口描述符 */
struct usb_intf_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   /* 0x04 */
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

/* USB 端点描述符 */
struct usb_ep_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   /* 0x05 */
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

/* 完整描述符结构 (FS + HS) */
struct ffs_descriptors {
    struct ffs_desc_v1_head head;
    /* Full-Speed (3 descriptors: 1 intf + 2 ep) */
    struct usb_intf_desc intf_fs;
    struct usb_ep_desc   ep_in_fs;   /* ep1: IN  (device→host) */
    struct usb_ep_desc   ep_out_fs;  /* ep2: OUT (host→device) */
    /* High-Speed (same structure) */
    struct usb_intf_desc intf_hs;
    struct usb_ep_desc   ep_in_hs;
    struct usb_ep_desc   ep_out_hs;
} __attribute__((packed));

static struct ffs_descriptors make_descriptors(uint8_t if_num,
                                               uint8_t ep_in_addr,
                                               uint8_t ep_out_addr)
{
    struct ffs_descriptors descriptors = {
        .head = {
            .magic    = LE32(FUNCTIONFS_DESCRIPTORS_MAGIC),
            .length   = LE32(sizeof(struct ffs_descriptors)),
            .fs_count = LE32(3),
            .hs_count = LE32(3),
        },
        .intf_fs = {
            .bLength            = sizeof(struct usb_intf_desc),
            .bDescriptorType    = 0x04,
            .bInterfaceNumber   = if_num,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 2,
            .bInterfaceClass    = 0xFF,
            .bInterfaceSubClass = 0x00,
            .bInterfaceProtocol = 0x00,
            .iInterface         = 0,
        },
        .ep_in_fs = {
            .bLength          = sizeof(struct usb_ep_desc),
            .bDescriptorType  = 0x05,
            .bEndpointAddress = ep_in_addr,
            .bmAttributes     = 0x02,
            .wMaxPacketSize   = LE16(64),
            .bInterval        = 0,
        },
        .ep_out_fs = {
            .bLength          = sizeof(struct usb_ep_desc),
            .bDescriptorType  = 0x05,
            .bEndpointAddress = ep_out_addr,
            .bmAttributes     = 0x02,
            .wMaxPacketSize   = LE16(64),
            .bInterval        = 0,
        },
        .intf_hs = {
            .bLength            = sizeof(struct usb_intf_desc),
            .bDescriptorType    = 0x04,
            .bInterfaceNumber   = if_num,
            .bAlternateSetting  = 0,
            .bNumEndpoints      = 2,
            .bInterfaceClass    = 0xFF,
            .bInterfaceSubClass = 0x00,
            .bInterfaceProtocol = 0x00,
            .iInterface         = 0,
        },
        .ep_in_hs = {
            .bLength          = sizeof(struct usb_ep_desc),
            .bDescriptorType  = 0x05,
            .bEndpointAddress = ep_in_addr,
            .bmAttributes     = 0x02,
            .wMaxPacketSize   = LE16(512),
            .bInterval        = 0,
        },
        .ep_out_hs = {
            .bLength          = sizeof(struct usb_ep_desc),
            .bDescriptorType  = 0x05,
            .bEndpointAddress = ep_out_addr,
            .bmAttributes     = 0x02,
            .wMaxPacketSize   = LE16(512),
            .bInterval        = 0,
        },
    };

    return descriptors;
}

/* ── strings: 0 语言, 0 字符串 ───────────────────────────────────────────── */
/* kernel 6.1: lang_count=0 works; lang_count=1 with str_count=0 → EINVAL  */
static const struct {
    struct usb_functionfs_strings_head head;
} __attribute__((packed)) strings = {
    .head = {
        .magic      = LE32(FUNCTIONFS_STRINGS_MAGIC),
        .length     = LE32(sizeof(strings)),
        .str_count  = LE32(0),
        .lang_count = LE32(0),
    },
};

/* ── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *ffs_dir = "/dev/usb-ffs/bulk1";
    uint8_t if_num = 0;
    uint8_t ep_in_addr = 0x83;
    uint8_t ep_out_addr = 0x02;
    if (argc >= 2) ffs_dir = argv[1];
    if (argc >= 3) if_num = (uint8_t)strtoul(argv[2], NULL, 0);
    if (argc >= 4) ep_in_addr = (uint8_t)strtoul(argv[3], NULL, 0);
    if (argc >= 5) ep_out_addr = (uint8_t)strtoul(argv[4], NULL, 0);

    struct ffs_descriptors descriptors = make_descriptors(if_num, ep_in_addr, ep_out_addr);

    char ep0_path[256];
    snprintf(ep0_path, sizeof(ep0_path), "%s/ep0", ffs_dir);

    printf("[ffs_init] 打开 %s\n", ep0_path);
    int fd = open(ep0_path, O_RDWR);
    if (fd < 0) {
        perror("open ep0");
        return 1;
    }

    /* 写描述符 */
    printf("[ffs_init] 写入 USB 描述符 (%zu bytes, if=%u in=0x%02X out=0x%02X)\n",
           sizeof(descriptors), if_num, ep_in_addr, ep_out_addr);
    ssize_t n = write(fd, &descriptors, sizeof(descriptors));
    if (n < 0) {
        perror("write descriptors");
        close(fd);
        return 1;
    }

    /* 写 strings — 部分内核 (sun50iw9 6.1) 写 strings 返回 EINVAL 但已进入 ACTIVE */
    n = write(fd, &strings, sizeof(strings));
    if (n < 0) {
        perror("write strings (non-fatal, continuing)");
        /* 不退出 — 描述符已写入，内核可能已接受 */
    }
    printf("[ffs_init] FunctionFS 进入 ACTIVE 状态\n");

    printf("[ffs_init] FunctionFS 描述符已写入，等待 UDC 绑定和 ENABLE 事件...\n");

    /* 循环处理 ep0 事件 (ENABLE / DISABLE / SETUP) */
    struct usb_functionfs_event event;
    while (1) {
        n = read(fd, &event, sizeof(event));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read ep0");
            break;
        }
        switch (event.type) {
        case FUNCTIONFS_ENABLE:
            printf("[ffs_init] ENABLE — M3E 已激活 Bulk 接口\n");
            break;
        case FUNCTIONFS_DISABLE:
            printf("[ffs_init] DISABLE\n");
            break;
        case FUNCTIONFS_SETUP:
            /* SETUP 请求：直接 stall（我们不处理控制请求） */
            write(fd, NULL, 0);
            break;
        case FUNCTIONFS_BIND:
            printf("[ffs_init] BIND — Gadget 已绑定 UDC\n");
            break;
        case FUNCTIONFS_UNBIND:
            printf("[ffs_init] UNBIND\n");
            goto done;
        default:
            printf("[ffs_init] 事件: %d\n", event.type);
            break;
        }
    }
done:
    close(fd);
    return 0;
}
