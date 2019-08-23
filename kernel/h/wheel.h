#ifndef WHEEL_H
#define WHEEL_H

#include <base.h>
#include <arch.h>
#include <config.h>

#include <core/spin.h>
#include <core/task.h>
#include <core/tick.h>
#include <core/work.h>
#include <core/sched.h>
#include <core/sema.h>
#include <core/file.h>

#include <mem/allot.h>
#include <mem/page.h>
#include <mem/pool.h>
#include <mem/kmem.h>

#include <drvs/kbd.h>

// #include <drvs/ios.h>
#include <drvs/pipe.h>
#include <drvs/tty.h>

#include <misc/ctype.h>
#include <misc/string.h>
#include <misc/vsprintf.h>

#include <misc/elf64.h>
#include <misc/debug.h>

#include <misc/list.h>
#include <misc/fifo.h>
#include <misc/kref.h>

#endif // WHEEL_H
