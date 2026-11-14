#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "kstats.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if (cpuid() == 0) {
    uint64 boot_start = r_time(), stage;
    ks_init();
    consoleinit();
    printkinit();
    printk("\n");
    printk("xv6 kernel is booting\n");
    printk("\n");
    stage = r_time();
    kinit();
    ks_boot(0, stage);
    stage = r_time();
    kvminit();
    kvminithart();
    ks_boot(1, stage);
    stage = r_time();
    procinit();
    ks_boot(2, stage);
    stage = r_time();
    trapinit();
    trapinithart();
    ks_boot(3, stage);
    stage = r_time();
    plicinit();
    plicinithart();
    ks_boot(4, stage);
    stage = r_time();
    binit();
    ks_boot(5, stage);
    stage = r_time();
    iinit();
    ks_boot(6, stage);
    stage = r_time();
    fileinit();
    ks_boot(7, stage);
    stage = r_time();
    virtio_disk_init();
    ks_boot(8, stage);
    userinit();
    ks_boot_end(boot_start);

    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging
    trapinithart(); // install kernel trap vector
    plicinithart(); // ask PLIC for device interrupts
  }

  scheduler();
}
