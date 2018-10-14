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
#include "userprog/process.h"
//#include "lib/kernel/list.h"
//struct lock
struct lock fileSystemLock;
static struct list FD;
static struct list returnStatusStruct;
typedef int pid_t;

/*

Todo:
1. Mark Threads Parent ID
2. Deny writes to .exe @ void file_deny_write @ src/filesys/file.h @ load, exe loaded we want to deny writes

*/

//helpers
static struct file *getFileFromFD(int fd, struct thread *);
//struct fileDescriptor *closeHelper(int fd, struct list *, bool, struct thread *);
static struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t);
static struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t);

//need to add locks to this
void setReturnStatus(tid_t threadID, int retStatus);
int getReturnStatus(tid_t threadID);
static bool isValidAddr(uint32_t *);

static void syscall_handler (struct intr_frame *);
static bool create(uint32_t *args);
static int write(uint32_t *args);
static int open(uint32_t *args);
static int read(uint32_t *args);
static unsigned tell (uint32_t *args);
static int wait(uint32_t *args); //int wait (pid_t);
static void seek(uint32_t *args);
static int filesize (uint32_t *args);
static void close(uint32_t *args);
static bool remove(uint32_t *args); //bool remove (const char *file);
static void exit(uint32_t *args);
static pid_t exec (uint32_t *args);
static void halt(void);


/*
Need to implement
*/


struct returnStatus {
  tid_t threadID;
  int retStatus;
  struct list_elem ret;
};

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
  list_init(&returnStatusStruct);
  lock_init(&fileSystemLock);
}




static void syscall_handler (struct intr_frame *f UNUSED) {

  uint32_t *args = ((uint32_t *) f->esp);

  if (!isValidAddr((void *) args)) {
    struct thread *cur = thread_current();
    printf("%s: exit(-1)\n", cur->name);
    thread_exit();
  }

  if (*args == SYS_HALT) {
    halt();
  } else if (*args == SYS_EXIT) {
    exit(args);
    thread_exit();
  } else if (*args == SYS_EXEC) {
    f->eax = exec(args);
  } else if (*args == SYS_WAIT) {
    f->eax = wait(args);
  } else if (*args == SYS_CREATE) {
    f->eax = create(args);
  } else if (*args == SYS_REMOVE) {
    f->eax = remove(args);
  } else if (*args == SYS_OPEN) {
    f->eax = open(args);
  } else if (*args == SYS_FILESIZE) {
    f->eax = filesize(args);
  } else if (*args == SYS_READ) {
    f->eax = read(args);
  } else if (*args == SYS_WRITE) {
    f->eax = write(args);
  } else if (*args == SYS_SEEK) {
    seek(args);
  } else if (*args == SYS_TELL) {
    f->eax = tell(args);
  } else if (*args == SYS_CLOSE) {
    close(args);
  }
}

static pid_t exec (uint32_t *args) {

  if (!isValidAddr((void *) args[1])) { return 0; }
  char *file = (char *) args[1];

  //the functions exec calls accesses the FS, we must acquire the FS lock
  lock_acquire(&fileSystemLock);

  tid_t childID = process_execute(file);

  lock_release(&fileSystemLock);

  return childID;

} //pid_t exec(const char *file);


static bool remove(uint32_t *args) { //bool remove (const char *file);
  if (!isValidAddr((void *) args[1]) || args == NULL) {
    exit(NULL);
    thread_exit();
  }

  char *file = (char *) args[1];
  lock_acquire(&fileSystemLock);
  bool result = filesys_remove(file);
  lock_release(&fileSystemLock);

  return result;
}

static bool create(uint32_t *args) {

  char *file = (char *) args[1];
  unsigned initial_size = (unsigned) args[2];


  if (!isValidAddr((void *) args[1]) || !isValidAddr((void *) file)) {
    //printf("BAD PTR\n");
    //return false;
    exit(NULL);
    thread_exit();
  }

  lock_acquire(&fileSystemLock);

  bool result = filesys_create(file, initial_size);

  lock_release(&fileSystemLock);

  return result;
}


static int wait(uint32_t *args) {
  pid_t id = (pid_t) args[1];
  return process_wait(id);
}

static void exit(uint32_t *args) {

  struct thread *cur = thread_current();
  int returnStatus = args == NULL ? -1 : (int)*(args + 1);

  if (cur->isWaitedOn == 1) {
    setReturnStatus(cur->tid, returnStatus);
  }

  printf("%s: exit(%d)\n", cur->name, returnStatus);
}

static void halt(void) {
  shutdown_power_off();
}


static int write(uint32_t *args) {

  if (!isValidAddr((void *) args[2]) || args[1] == 0 || args[1] == 2) {
    exit(NULL);
    thread_exit();
  }
  int fd = (int) args[1];
  char* buffer = (char *) args[2];
  unsigned size = (unsigned) args[3];

  lock_acquire(&fileSystemLock);

  if (fd == 1) {
    putbuf(buffer, size);
  } else {
    struct file* file = getFileFromFD(fd, thread_current());
    if (file == NULL) {
      lock_release(&fileSystemLock);
      return 0;
    }
    size = file_write(file, buffer, size);
  }
  lock_release(&fileSystemLock);

  return size;
}

static void close(uint32_t *args) {

  struct thread *t = thread_current();
  int closeFD = (int) args[1];
  struct fileDescriptor *fdStruct = NULL;

  lock_acquire(&fileSystemLock);

  if (closeHelperThread(closeFD, &t->fdList, t) && (fdStruct = closeHelperGlobal(closeFD, &FD, t)) != NULL) {
    file_close(fdStruct->file);
    //if (fdStruct->fd < t->lowestOpenFD)
    //  t->lowestOpenFD = fdStruct->fd;
    free(fdStruct);
  }

  lock_release(&fileSystemLock);
}

static int open(uint32_t *args) {

  const char *name = (char *)args[1];
  int setFD = -1;
  struct file *f = NULL;

  if (!isValidAddr((void *) args[1])) {
    exit(NULL);
    thread_exit();
  }

  lock_acquire(&fileSystemLock);
  if ((f = filesys_open(name)) != NULL) {
    struct thread *t = thread_current();
    struct fileDescriptor *fileDesc = malloc(sizeof(struct fileDescriptor));
    setFD = ++t->lowestOpenFD;
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

static void seek(uint32_t *args) {
  int fd = (int) args[1];
  unsigned position = (unsigned) args[2];
  struct file *fp = NULL;

  lock_acquire(&fileSystemLock);

  fp = getFileFromFD(fd, thread_current());

  if (fp != NULL) {
    file_seek(fp, position);
  }

  lock_release(&fileSystemLock);
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

static int read(uint32_t *args) {
 //int read(int fd, void *buffer, unsigned length);
 //pass tests/userprog/read-stdout
 //pass tests/userprog/read-bad-fd

 //||
 //FAIL tests/userprog/read-stdout
 //FAIL tests/userprog/read-bad-fd

 int fd = (int) args[1];
 void *buffer = (void *) args[2];
 unsigned length = (unsigned) args[3];
 struct file *fp = NULL;

 if (!isValidAddr(buffer) || fd == 1 || fd == 2) {
   exit(NULL);
   thread_exit();
 }
 lock_acquire(&fileSystemLock);
 fp = getFileFromFD(fd, thread_current());
 if (fp == NULL) {
   lock_release(&fileSystemLock);
   return 0;
 }
 int bytesRead = file_read(fp, buffer, (uint32_t) length);
 lock_release(&fileSystemLock);

 return bytesRead;
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

void setReturnStatus(tid_t threadID, int retStatus) {
  struct returnStatus *rs = malloc(sizeof(struct returnStatus));
  //trash error handling
  if (rs != NULL) {
    rs->threadID = threadID;
    rs->retStatus = retStatus;
    list_push_back(&returnStatusStruct, &rs->ret);
  }
}

int getReturnStatus(tid_t threadID) {
  struct list_elem *iter;
  for (iter = list_begin(&returnStatusStruct); iter != list_end(&returnStatusStruct); iter = list_next(iter)) {
    struct returnStatus *rsStruct = list_entry(iter, struct returnStatus, ret);
    if (rsStruct->threadID == threadID) {
      int returnValue = rsStruct->retStatus;
      list_remove(&rsStruct->ret);
      free(rsStruct);
      return returnValue;
    }
  }
  return -1;
}


static bool isValidAddr(uint32_t *vaddr) {

  struct thread *cur = thread_current();
  //check if its a user address
  // int *PHYS_BASE = (int *)0xC0000000;
  // 0xC0000000 > vaddr
  return vaddr != NULL && is_user_vaddr(vaddr) && pagedir_get_page(cur->pagedir,(void *) vaddr);
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
