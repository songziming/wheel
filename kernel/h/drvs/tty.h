#ifndef DRVS_TTY_H
#define DRVS_TTY_H

#include <base.h>

typedef struct iodev iodev_t;

extern iodev_t * tty_get_instance();

extern __INIT void tty_dev_init();

#endif // DRVS_TTY_H