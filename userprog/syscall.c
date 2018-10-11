#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
//#include "lib/kernel/list.h"
//struct lock

static void syscall_handler (struct intr_frame *);
static int write(uint32_t *args);
static int open(uint32_t *args);
static void close(uint32_t *args);
static void exit(uint32_t *args);
static bool isValidAddr(uint32_t *);
static void halt(void);

static struct list FD;


struct fileDescriptor {
  int fd;
  struct file *file;
  struct list_elem globalFDList;
  struct list_elem threadFDList;
};



void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&FD);
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

  } else if (*args == SYS_FILESIZE) {

  } else if (*args == SYS_READ) {

  } else if (*args == SYS_WRITE) {
    f->eax = write(args);
  } else if (*args == SYS_SEEK) {

  } else if (*args == SYS_TELL) {

  } else if (*args == SYS_CLOSE) {

  }
}

static void exit(uint32_t *args) {
  struct thread *cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, *(args + 1));
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
}

static int open(uint32_t *args) {

  const char *name = (char *)args[1];
  int setFD = -1;
  struct file *f = NULL;

  if (!isValidAddr((void *)args[1]) && (f = filesys_open(name)) != NULL) {
    //Acquire FS lock
    struct thread *t = thread_current();
    struct fileDescriptor *fileDesc = malloc(sizeof(struct fileDescriptor));
    setFD = t->lowestOpenFD++;

    ASSERT(setFD != 0 || setFD != 1);

    fileDesc->fd = setFD;
    fileDesc->file = f;

    list_push_back(&t->fdList, &fileDesc->threadFDList);
    list_push_back(&FD ,&fileDesc->globalFDList);
    //Release FS lock

  }
  return setFD;

}

static void halt(void) {
  shutdown_power_off();
}

static bool isValidAddr(uint32_t *vaddr) {

  struct thread *cur = thread_current();
  //check its a user address
  // int *PHYS_BASE = (int *)0xC0000000;
  // 0xC0000000 > vaddr
  return is_user_vaddr(vaddr) && pagedir_get_page(cur->pagedir,(void *) vaddr);
}
