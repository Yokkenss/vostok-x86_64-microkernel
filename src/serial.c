#include "serial.h"
#include "kernel/process.h"
#include "mm/address_space.h"
#include "panic.h"
#include "print.h"
#include "x86/io.h"
#include "x86/timer.h"

#include <stdint.h>

enum {
    COM1 = 0x3f8,
    IOAPIC_REGISTER_SELECT = 0x00,
    IOAPIC_WINDOW = 0x10,
    IOAPIC_REDIRECTION_BASE = 0x10,
    UART_IRQ = 4,
    UART_VECTOR = 33,
};

static const uint64_t IOAPIC_VIRTUAL = 0xffffffff90001000ULL;
static volatile uint32_t *ioapic;
static uint8_t receive_buffer[64];
static unsigned int receive_read;
static unsigned int receive_write;
static unsigned long interrupt_count;

static uint32_t ioapic_read(uint8_t index)
{
    ioapic[IOAPIC_REGISTER_SELECT / 4] = index;
    return ioapic[IOAPIC_WINDOW / 4];
}

static void ioapic_write(uint8_t index, uint32_t value)
{
    ioapic[IOAPIC_REGISTER_SELECT / 4] = index;
    ioapic[IOAPIC_WINDOW / 4] = value;
}

static void buffer_received_character(uint8_t character)
{
    const unsigned int next = (receive_write + 1) % sizeof(receive_buffer);
    if (next == receive_read) return;
    receive_buffer[receive_write] = character;
    receive_write = next;
}

void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* Disable UART interrupts. */
    outb(COM1 + 3, 0x80); /* Make the baud-rate divisor accessible. */
    outb(COM1 + 0, 0x01); /* Divisor 1: 115200 baud. */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* Eight data bits, no parity, one stop bit. */
    outb(COM1 + 2, 0xc7); /* Enable and clear FIFO, fourteen-byte threshold. */
    outb(COM1 + 4, 0x03); /* Data-terminal-ready and request-to-send. */
}

void serial_putc(char character)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
    }
    outb(COM1, (uint8_t)character);
}

void serial_write(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

int serial_try_getc(void)
{
    if (receive_read != receive_write) {
        const uint8_t character = receive_buffer[receive_read];
        receive_read = (receive_read + 1) % sizeof(receive_buffer);
        return character;
    }
    if ((inb(COM1 + 5) & 0x01) == 0) return -1;
    return inb(COM1);
}

void serial_irq_init(void)
{
    struct address_space kernel_space = address_space_current();
    ASSERT(address_space_map_kernel(&kernel_space, IOAPIC_VIRTUAL,
                                    0xfec00000, PAGE_WRITABLE));
    ioapic = (volatile uint32_t *)(uintptr_t)IOAPIC_VIRTUAL;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(1), "c"(0));
    (void)eax; (void)ecx; (void)edx;
    const uint8_t destination = (uint8_t)(ebx >> 24);
    const uint8_t redirection = IOAPIC_REDIRECTION_BASE + UART_IRQ * 2;
    ioapic_write(redirection + 1, (uint32_t)destination << 24);
    ioapic_write(redirection, UART_VECTOR);

    outb(COM1 + 4, 0x0b);
    outb(COM1 + 1, 0x01);
    log_info("UART IRQ routed: GSI %u -> vector %u, IOAPIC version 0x%x",
             UART_IRQ, UART_VECTOR, ioapic_read(1));
}

struct exception_frame *serial_dispatch(struct exception_frame *frame)
{
    ++interrupt_count;
    while ((inb(COM1 + 5) & 0x01) != 0)
        buffer_received_character(inb(COM1));
    process_serial_wake();
    timer_end_interrupt();
    return frame;
}

unsigned long serial_interrupt_count(void)
{
    return interrupt_count;
}
