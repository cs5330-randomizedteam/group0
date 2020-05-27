#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool is_valid(uint32_t *pd, void *uaddr) {
  return uaddr != NULL && is_user_vaddr(uaddr) && pagedir_get_page(pd, uaddr) != NULL;
}

static bool page_fault_exit(struct intr_frame *f) {
  // clean up resources

  // set return code to -1 and exit.
  f->eax = -1;
  printf ("%s: exit(%d)\n", &thread_current ()->name, -1);
  thread_exit ();
}

static void check_valid_uaddr(struct intr_frame *f, void *uaddr, size_t len) 
{
  uint32_t *pd = thread_current()->pagedir;

  // check all byte addresses are valid.
  void *end_addr = uaddr + len;
  bool flag = is_valid(pd, end_addr);
  for (; uaddr <= end_addr; uaddr += PGSIZE) {
    if (!flag) break;
    flag = flag && is_valid(pd, uaddr);
  }
  if (flag) return;
  page_fault_exit(f);
}

static void validate_char_str(const char* start_addr, struct intr_frame *f) {
  uint32_t *pd = thread_current()->pagedir;
  for (;; ++start_addr) {
    if (!is_valid(pd, (void*)start_addr)) page_fault_exit(f);
    if (*start_addr == '\0') break;
  }
  return;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  check_valid_uaddr (f, f->esp, sizeof(uint32_t));
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
        check_valid_uaddr(f, args + 1, sizeof(uint32_t));
        f->eax = args[1];
        printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
        thread_exit ();
        break;

      case SYS_PRACTICE:
        check_valid_uaddr(f, args + 1, sizeof(uint32_t));
        f->eax = args[1] + 1; 
        break;

      case SYS_WRITE:
        { 
          check_valid_uaddr(f, args + 1, 3 * sizeof(uint32_t));
          int fd = args[1];
          const char *buf = (const char*)args[2];
          size_t size = args[3];

          // STDOUT
          if (fd == 1) putbuf(buf, size);
          break;
        }
      case SYS_HALT:
        shutdown_power_off();
        break;

      case SYS_EXEC:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* start_addr = (char*)args[1];
          validate_char_str(start_addr, f);

          // process_wait (process_execute (start_addr));
          break;
        }

      case SYS_WAIT:
        break;

      case SYS_CREATE:
        {
          check_valid_uaddr(f, args + 1, 2 * sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);
          uint32_t init_size = args[2];
          bool success = filesys_create(filename, init_size);
          f->eax = success;
          break;
        }

      case SYS_REMOVE:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);
          bool success = filesys_remove(filename);
          f->eax = success;
          break;
        }      

      case SYS_OPEN:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);
          struct file* opened_file = filesys_open(filename);
          if (opened_file == NULL) {
            f->eax = -1;
            break;
          }

          struct file** cur_fdtable = thread_current()->fdtable; 
          int i;

          // fd 0 & 1 reserved for STDIN & STDOUT.
          for (i = 2; i < MAX_FILE_DESCRIPTORS; ++i) {
            if (cur_fdtable[i] == NULL) {
              cur_fdtable[i] = opened_file;
              f->eax = i;
              break;
            }
          }

          // Too many opened files, not enough space.
          if (i == MAX_FILE_DESCRIPTORS) f->eax = -1;
          break;
        }

      case SYS_CLOSE:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          int fd = args[1];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) break;

          struct file** cur_fdtable = thread_current()->fdtable;
          file_close(cur_fdtable[fd]);
          cur_fdtable[fd] = NULL;
          break;
        }

      case SYS_FILESIZE:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          int fd = args[1];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            f->eax = 0;
            break;
          }

          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL) {
            f->eax = 0;
            break;
          }

          f->eax = file_length(cur_file);
          break;
        }
    }
}
