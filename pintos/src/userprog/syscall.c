#include "userprog/syscall.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "filesys/fsutil.h"
#include "lib/user/syscall.h"

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
  thread_current ()->exit_status = -1;
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
        printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
        thread_current ()-> exit_status = args[1];
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
          char* buf = (char*) args[2];
          uint32_t size = args[3];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            f->eax = 0;
            break;
          }

          // write to stdout
          if (fd == 1) {
            check_valid_uaddr(f, buf, size);
            putbuf(buf, size);
            f->eax = size;
            break;
          }

          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL) {
            f->eax = 0;
            break;
          }

          uint32_t remain_size = file_length(cur_file) - file_tell(cur_file);
          uint32_t max_write_size = remain_size < size ? remain_size : size;
          check_valid_uaddr(f, buf, max_write_size);

          uint32_t write_size = file_write(cur_file, buf, size);
          f->eax = write_size;
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

          tid_t tid = process_execute(start_addr);
          if (tid == TID_ERROR) {
            f->eax = tid;
            break;
          }

          struct thread* t = get_thread(tid);
          ASSERT(t != NULL);

          sema_down(&(t->load_sem));
          if (t->is_loaded) {
            f->eax = tid;
          } else {
            f->eax = TID_ERROR;
          }
          break;
        }

      case SYS_WAIT:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          tid_t tid = args[1];
          f->eax = process_wait(tid); 
          break;
        } 

      case SYS_CREATE:
        {
          check_valid_uaddr(f, args + 1, 2 * sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);


          int split_idx = fsutil_split_path(filename);
          char* new_filename = filename + split_idx + 1;
          
          if (strlen(new_filename) == 0) {
            f->eax = -1;
            break;
          }

          struct dir* working_dir = dir_resolve(filename);

          uint32_t init_size = args[2];
          bool success = filesys_create(filename, init_size, working_dir);
          dir_close(working_dir);
          f->eax = success;
          break;
        }

      case SYS_REMOVE:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);

          int split_idx = fsutil_split_path(filename);
          char* new_filename = filename + split_idx + 1;
          
          if (strlen(new_filename) == 0) {
            f->eax = -1;
            break;
          }

          struct dir* working_dir = dir_resolve(filename);

          bool success = filesys_remove(new_filename, working_dir);
          dir_close(working_dir);
          f->eax = success;
          break;
        }      

      case SYS_OPEN:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* filename = (char*)args[1];
          validate_char_str(filename, f);

          int split_idx = fsutil_split_path(filename);
          char* new_filename = filename + split_idx + 1;
          
          if (strlen(new_filename) == 0) {
            f->eax = -1;
            break;
          }

          struct dir* working_dir = dir_resolve(filename);

          struct file* opened_file = filesys_open(new_filename, working_dir);
          dir_close(working_dir);
          if (opened_file == NULL) {
            f->eax = -1;
            break;
          }

          // executable running
          if (get_thread_with_name(filename) != NULL) file_deny_write(opened_file);

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

      case SYS_INUMBER:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          int fd = args[1];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            f->eax = -1;
            break;
          } 
          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL) {
            f->eax = -1;
            break;
          }

          f->eax = inode_get_inumber(file_get_inode(cur_file));
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

      case SYS_TELL:
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

          f->eax = file_tell(cur_file);
          break;
        }
      case SYS_SEEK:
        {
          check_valid_uaddr(f, args + 1, 2 * sizeof(uint32_t));
          int fd = args[1];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            break;
          }
          uint32_t pos = args[2];

          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL) {
            break;
          }

          file_seek(cur_file, pos);
          break;          
        }

      case SYS_READ:
        {
          check_valid_uaddr(f, args + 1, 3 * sizeof(uint32_t));
          int fd = args[1];
          char* buf = (char*) args[2];
          uint32_t size = args[3];
          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            f->eax = -1;
            break;
          }

          // read from stdin
          if (fd == 0) {
            check_valid_uaddr(f, buf, size);
            while (size > 0) {
              *buf = input_getc();
              buf++;
              size--;
            }
            f->eax = size;
            break;
          }

          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL) {
            f->eax = -1;
            break;
          }

          uint32_t remain_size = file_length(cur_file) - file_tell(cur_file);
          uint32_t min_buf_size = remain_size < size ? remain_size : size;
          check_valid_uaddr(f, buf, min_buf_size);

          uint32_t read_size = file_read(cur_file, buf, size);
          ASSERT(read_size == min_buf_size);
          f->eax = read_size;
          break;
        }

      case SYS_CHDIR:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* dir_name = (char*)args[1];
          validate_char_str(dir_name, f);

          struct dir* cur_dir = dir_resolve (dir_name);
          if (cur_dir == NULL) f->eax = 0;
          else {
            thread_current()->dir_sector = inode_get_inumber(dir_get_inode(cur_dir));
            f->eax = 1;
          }
          dir_close(cur_dir);
          break;
        }

      case SYS_MKDIR:
        {
          check_valid_uaddr(f, args + 1, sizeof(uint32_t));
          char* dir_name = (char*)args[1];
          validate_char_str(dir_name, f);

          int split_idx = fsutil_split_path(dir_name);
          char* new_dir_name = dir_name + split_idx + 1;
          
          if (strlen(new_dir_name) == 0) {
            f->eax = 0;
            break;
          }
        
          struct dir* parent_dir = dir_resolve (dir_name);

          if (parent_dir == NULL) {
            f->eax = 0;
            break;
          }

          block_sector_t sector;
          if (free_map_allocate(1, &sector)) {
            block_sector_t parent_sector = inode_get_inumber(dir_get_inode(parent_dir));
            dir_create (sector, 16, parent_sector);
            dir_add (parent_dir, new_dir_name, sector);
            f->eax = 1;
          } else {
            f->eax = 0;
          }

          dir_close(parent_dir);
          break;
        }

      case SYS_READDIR:
        {
          check_valid_uaddr(f, args + 1, 2 * sizeof(uint32_t));
          int fd = args[1];
          char* name = (char*)args[2];
          check_valid_uaddr(f, name, READDIR_MAX_LEN + 1);

          if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS) {
            f->eax = 0;
            break;
          }

          struct file* cur_file = thread_current()->fdtable[fd];
          if (cur_file == NULL || !inode_is_dir(file_get_inode(cur_file))) {
            f->eax = 0;
            break;
          }

          struct dir* cur_dir = dir_open(file_get_inode(cur_file));
          f->eax = dir_readdir (cur_dir, name);
          dir_close(cur_dir);
          break;
        }

      case SYS_ISDIR:
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

          f->eax = inode_is_dir(file_get_inode(cur_file));
          break;
        }

    }
}
