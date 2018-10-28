#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

struct returnStatus {
  tid_t threadID;
  int retStatus;
  struct list_elem ret;
};

struct fileDescriptor {
  int fd;
  struct file *file;
  struct thread *t;
  //struct mmap_file *mmap;
  struct list_elem globalFDList;
  struct list_elem threadFDList;
};

#ifdef VM
typedef int mapid_t;
static int mmapID = 0;
static struct list _mmapList;
struct lock _mmapLock;

struct mmap_file {
  void *base;
  int fd;
  int pages_taken;
  mapid_t m_id;
  struct thread *owner;
  struct list_elem elem;
};

#endif

void syscall_init (void);

struct file *getFileFromFD(int fd, struct thread *);
struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t);
struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t);

void setReturnStatus(tid_t threadID, int retStatus);
int getReturnStatus(tid_t threadID);

#endif /* userprog/syscall.h */

// enum {
//     SYS_HALT,                   /* Halt the operating system. */
//     SYS_EXIT,                   /* Terminate this process. */
//     SYS_EXEC,                   /* Start another process. */
//     SYS_WAIT,                   /* Wait for a child process to die. */
//     SYS_CREATE,                 /* Create a file. */
//     SYS_REMOVE,                 /* Delete a file. */
//     SYS_OPEN,                   /* Open a file. */
//     SYS_FILESIZE,               /* Obtain a file's size. */
//     SYS_READ,                   /* Read from a file. */
//     SYS_WRITE,                  /* Write to a file. */
//     SYS_SEEK,                   /* Change position in a file. */
//     SYS_TELL,                   /* Report current position in a file. */
//     SYS_CLOSE,                  /* Close a file. */
//
//     /* Project 3 and optionally project 4. */
//     SYS_MMAP,                   /* Map a file into memory. */
//     SYS_MUNMAP,                 /* Remove a memory mapping. */
//
//     /* Project 4 only. */
//     SYS_CHDIR,                  /* Change the current directory. */
//     SYS_MKDIR,                  /* Create a directory. */
//     SYS_READDIR,                /* Reads a directory entry. */
//     SYS_ISDIR,                  /* Tests if a fd represents a directory. */
//     SYS_INUMBER                 /* Returns the inode number for a fd. */
//   };
