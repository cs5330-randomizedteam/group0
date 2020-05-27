#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void check_valid_uaddr(struct intr_frame *f, void *uaddr, size_t len) 
{
  uint32_t *pd = thread_current()->pagedir;

  // check all byte addresses are valid.
  int i;
  bool flag = true;
  for (i = 0; i < len; ++i, uaddr+=4) {
    if (!(uaddr != NULL && is_user_vaddr(uaddr) && pagedir_get_page(pd, uaddr) != NULL && 
      is_user_vaddr(uaddr + 3) && pagedir_get_page(pd, uaddr + 3) != NULL)) flag = false;
  }
  if (flag) return;

  // clean up resources

  // set return code to -1 and exit.
  f->eax = -1;
  printf ("%s: exit(%d)\n", &thread_current ()->name, -1);
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  check_valid_uaddr (f, f->esp, 1);
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

   // printf("System call number: %d\n", args[0]); 

  switch(args[0]) 
    {
      case SYS_EXIT:
        check_valid_uaddr(f, args + 1, 1);
        f->eax = args[1];
        printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
        thread_exit ();
        break;

      case SYS_PRACTICE:
        check_valid_uaddr(f, args + 1, 1);
        f->eax = args[1] + 1; 
        break;

      case SYS_WRITE:
        { 
          check_valid_uaddr(f, args + 1, 3);
          int fd = args[1];
          const char *buf = (const char*)args[2];
          size_t size = args[3];

          // STDOUT
          if (fd == 1) putbuf(buf, size);
          break;
        }
      case SYS_HALT:
        shutdown_power_off();
      
    }
}
