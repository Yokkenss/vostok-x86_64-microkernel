#ifndef VOSTOK_SERIAL_H
#define VOSTOK_SERIAL_H

void serial_init(void);
void serial_putc(char character);
void serial_write(const char *text);
int serial_try_getc(void);
void serial_irq_init(void);
unsigned long serial_interrupt_count(void);

#endif
