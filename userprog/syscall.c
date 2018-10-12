#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
//#include "lib/kernel/list.h"
//struct lock
struct lock fileSystemLock;
static struct list FD;


//helpers
static struct file *getFileFromFD(int fd, struct thread *);
//struct fileDescriptor *closeHelper(int fd, struct list *, bool, struct thread *);
static struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t);
static struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t);
static bool isValidAddr(uint32_t *);

static void syscall_handler (struct intr_frame *);
static int write(uint32_t *args);
static int open(uint32_t *args);
static unsigned tell (uint32_t *args);
static int filesize (uint32_t *args);
static void close(uint32_t *args);
static void exit(uint32_t *args);
static void halt(void);


struct fileDescriptor {
  int fd;
  struct file *file;
  struct thread *t;
  struct list_elem globalFDList;
  struct list_elem threadFDList;
};



void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&FD);
  lock_init(&fileSystemLock);
}




static void syscall_handler (struct intr_frame *f UNUSED) {

  uint32_t *args = ((uint32_t *) f->esp);

  if (*args == SYS_HALT) {
    halt();
  } else if (*args == SYS_EXIT) {
    exit(args);
    thread_exit();
  } else if (*args == SYS_EXEC) {

  } else if (*args == SYS_WAIT) {

  } else if (*args == SYS_CREATE) {

  } else if (*args == SYS_REMOVE) {

  } else if (*args == SYS_OPEN) {
    f->eax = open(args);
  } else if (*args == SYS_FILESIZE) {
    f->eax = filesize(args);
  } else if (*args == SYS_READ) {

  } else if (*args == SYS_WRITE) {
    f->eax = write(args);
  } else if (*args == SYS_SEEK) {

  } else if (*args == SYS_TELL) {
    f->eax = tell(args);
  } else if (*args == SYS_CLOSE) {
    close(args);
  }
}

static void exit(uint32_t *args) {
  struct thread *cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, *(args + 1));
}

static void halt(void) {
  shutdown_power_off();
}


static int write(uint32_t *args) {

  if (!isValidAddr((void *) args[2]) || args[1] == 0) { return 0; }
  int fd = (int) args[1];
  char* buffer = (char *) args[2];
  unsigned size = (unsigned) args[3];

  if (fd == 1) {
    putbuf(buffer, size);
  }
  return size;
}

static void close(uint32_t *args) {

  struct thread *t = thread_current();
  int closeFD = (int) args[1];
  struct fileDescriptor *fdStruct = NULL;

  lock_acquire(&fileSystemLock);

  if (closeHelperThread(closeFD, &t->fdList, t) && (fdStruct = closeHelperGlobal(closeFD, &FD, t)) != NULL) {
    file_close(fdStruct->file);
    if (fdStruct->fd < t->lowestOpenFD)
      t->lowestOpenFD = fdStruct->fd;
    free(fdStruct);
  }

  lock_release(&fileSystemLock);
}

static int open(uint32_t *args) {

  const char *name = (char *)args[1];
  int setFD = -1;
  struct file *f = NULL;

  lock_acquire(&fileSystemLock);
  if (!isValidAddr((void *)args[1]) && (f = filesys_open(name)) != NULL) {
    struct thread *t = thread_current();
    struct fileDescriptor *fileDesc = malloc(sizeof(struct fileDescriptor));
    setFD = t->lowestOpenFD++;

    ASSERT(setFD != 0 || setFD != 1);

    fileDesc->t = t;
    fileDesc->fd = setFD;
    fileDesc->file = f;

    //implicitly protected
    list_push_back(&t->fdList, &fileDesc->threadFDList);
    list_push_back(&FD ,&fileDesc->globalFDList);

  }
  lock_release(&fileSystemLock);

  return setFD;
}

static unsigned tell (uint32_t *args) {
  int fd = (int) args[1];
  unsigned nextByte = 0;
  struct file *fp = NULL;

  lock_acquire(&fileSystemLock);

  fp = getFileFromFD(fd, thread_current());

  if (fp != NULL) {
    nextByte = file_tell(fp);
  }

  lock_release(&fileSystemLock);
  return nextByte;
}

static int filesize (uint32_t *args) {
  int fd = (int) args[1];
  int fileSize = 0;

  struct file *fp = NULL;

  lock_acquire(&fileSystemLock);
  if ((fp = getFileFromFD(fd, thread_current())) != NULL) {
    fileSize = file_length(fp);
  }
  lock_release(&fileSystemLock);

  return fileSize;

}

//Helper Functions
static bool isValidAddr(uint32_t *vaddr) {

  struct thread *cur = thread_current();
  //check its a user address
  // int *PHYS_BASE = (int *)0xC0000000;
  // 0xC0000000 > vaddr
  return is_user_vaddr(vaddr) && pagedir_get_page(cur->pagedir,(void *) vaddr);
}

static struct file *getFileFromFD(int fd, struct thread *t) {
  //assume lock as been acquired
  struct list_elem *iter;
  for (iter = list_begin(&t->fdList); iter != list_end(&t->fdList); iter = list_next(iter)) {
    struct fileDescriptor *fdStruct = list_entry(iter, struct fileDescriptor, threadFDList);
    if (fdStruct->fd == fd && fdStruct->t == t)
      return fdStruct->file;
  }
  return NULL;
}

/* need 2 functions due to
../../userprog/syscall.c:211: error: ‘struct fileDescriptor’ has no member named ‘global’
../../userprog/syscall.c:211: error: ‘globalFDList’ undeclared (first use in this function)
../../userprog/syscall.c:211: error: (Each undeclared identifier is reported only once
../../userprog/syscall.c:211: error: for each function it appears in.)
../../userprog/syscall.c:211: error: ‘threadFDList’ undeclared (first use in this function)
*/
static struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t) {
  // assume lock has been acquired
  struct list_elem *iter;
  for (iter = list_begin(lst); iter != list_end(lst); iter = list_next(iter)) {
    struct fileDescriptor *fdStruct = list_entry(iter, struct fileDescriptor, globalFDList);
    if (fdStruct->fd == fd && fdStruct->t == t) {
      list_remove(&fdStruct->globalFDList);
      return fdStruct;
    }
  }
  return NULL;
}

static struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t) {
  //assume lock has been acquired
  struct list_elem *iter;
  for (iter = list_begin(lst); iter != list_end(lst); iter = list_next(iter)) {
    struct fileDescriptor *fdStruct = list_entry(iter, struct fileDescriptor, threadFDList);
    if (fdStruct->fd == fd && fdStruct->t == t) {
      list_remove(&fdStruct->threadFDList);
      return fdStruct;
    }
  }
  return NULL;
}
