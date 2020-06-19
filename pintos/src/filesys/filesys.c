#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "filesys/fsutil.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  cache_init();
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) do_format ();


  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  cache_flush(fs_device);
  free_map_close ();
}

static void
filesys_resolve_path(const char* path, char** filename, char** name, struct dir** working_dir) 
{
  int len = strlen(path);
  *name = malloc(len + 1);
  if (*name == NULL) return;
  strlcpy(*name, path, len + 1);
  int split_idx = fsutil_split_path(*name);
  *filename = *name + split_idx + 1;
  char * dir_name = *name;
  if (split_idx == -1) dir_name = "";
  if (split_idx == 0) *working_dir = dir_open_root();
  else *working_dir = dir_resolve(dir_name);
}



/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size)
{   
  char* filename = NULL, *name = NULL;
  struct dir* working_dir = NULL;

  filesys_resolve_path(path, &filename, &name, &working_dir);

  block_sector_t inode_sector = 0;
  bool success = (working_dir != NULL
                  && !inode_isremoved(dir_get_inode(working_dir))
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (working_dir, filename, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (working_dir);

  free(name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct gfile
filesys_open (const char *path)
{ 
  struct gfile res;
  if (strcmp(path, "/") == 0) {
    res.content = dir_open_root();
    res.is_dir = 1;
    return res;
  }

  struct inode *inode = NULL;
  char* filename = NULL, *name = NULL;
  struct dir* working_dir;

  filesys_resolve_path(path, &filename, &name, &working_dir);
  if (working_dir != NULL)
    dir_lookup (working_dir, filename, &inode);

  if (inode == NULL || inode_isremoved(dir_get_inode(working_dir))) {
    res.content = NULL;
    return res;
  }

  dir_close(working_dir);
  free(name);

  if (inode_isdir(inode)) {
    res.content = dir_open(inode);
    res.is_dir = 1;
  } else {
    res.content = file_open(inode);
    res.is_dir = 0;
  }
  return res;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path)
{
  char* filename = NULL, *name = NULL;
  struct dir* working_dir = NULL;

  filesys_resolve_path(path, &filename, &name, &working_dir);

  bool success = working_dir != NULL && dir_remove (working_dir, filename);
  dir_close(working_dir);

  free(name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
