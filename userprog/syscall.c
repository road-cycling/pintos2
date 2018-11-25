#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"


#ifdef VM
#include "lib/round.h"
#include "vm/frame.h"
#include "vm/page.h"
#endif

typedef int pid_t;

struct returnStatus {
  tid_t threadID;
  int retStatus;
  struct list_elem ret;
};

struct fileDescriptor {
  int fd;
  struct file *file;
  struct thread *t;
  struct mmap_file *mmap;
  struct list_elem globalFDList;
  struct list_elem threadFDList;
};

struct lock fileSystemLock;
static struct list FD;
static struct list returnStatusStruct;

#ifdef VM
int mmapID = 0;
static struct list _mmapList;
struct lock mmap_id_lock;
struct lock _mmapLock;

// struct mmap_file {
//   void *base;
//   int fd;
//   int pages_taken;
//   mapid_t m_id;
//   struct thread *owner;
//   struct list_elem elem;
// };


struct mmap_file*_find_mmap_by_thread(struct thread *t);
struct mmap_file *_findMmapFile(mapid_t mid);
#endif

static void syscall_handler (struct intr_frame *);



static bool isValidAddr(uint32_t *);
static bool create(uint32_t *args);
static int write(uint32_t *args);
static int open(uint32_t *args);
static int read(uint32_t *args);
static unsigned tell (uint32_t *args);
static int wait(uint32_t *args);
static void seek(uint32_t *args);
static int filesize (uint32_t *args);
static void close(uint32_t *args);
static bool remove(uint32_t *args);
static void exit(uint32_t *args);
static pid_t exec (uint32_t *args);
static void halt(void);

#ifdef VM
static mapid_t mmap(uint32_t *args);
static void munmap (uint32_t *args);
static bool is_v_mmap_addr(uint32_t *vaddr);
#endif

/*
Need to implement
*/

void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&FD);
  list_init(&returnStatusStruct);
#ifdef VM
  list_init(&_mmapList);
  lock_init(&_mmapLock);
  lock_init(&mmap_id_lock);
#endif
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
#ifdef VM
  else if (*args == SYS_MMAP) {
     f->eax = mmap(args);
  } else if (*args == SYS_MUNMAP) {
     munmap(args);
  }
#endif
}

static pid_t exec (uint32_t *args) {

  if (!isValidAddr((void *) args[1])) { return 0; }
  char *file = (char *) args[1];

  //the functions exec calls accesses the FS, we must acquire the FS lock
  lock_acquire(&fileSystemLock);

  tid_t childID = process_execute(file);
  lock_release(&fileSystemLock);

  return childID;

}


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
  //need to copy buffer into memory
  char* buffer = (char *) args[2];
  unsigned size = (unsigned) args[3];

  // printf("FD: %d\t Size: %d\n", fd, size);

  lock_acquire(&fileSystemLock);
#ifdef VM
  if (fd == 1) {
    putbuf(buffer, size);
  } else {
    struct fileDescriptor *s_fd = getFD(fd, thread_current());
    if (s_fd == NULL) {
      lock_release(&fileSystemLock);
      return 0;
    }
    // write to file
    if (s_fd->mmap == NULL) {
      size = file_write(s_fd->file, buffer, size);
    } else {
      if (size > (unsigned)s_fd->mmap->file_size) {
        lock_release(&fileSystemLock);
        return -1;
      }
      int i = 0;
      int numPages = DIV_ROUND_UP(size, PGSIZE);
      // memcpy (destination, source, length)
      for (; i < numPages; i++) {
        if (i == numPages - 1) {
          pg_mark_dirty(s_fd->mmap->base + i * PGSIZE, (unsigned)s_fd->mmap->file_size - i * PGSIZE);
          memcpy(s_fd->mmap->base + i * PGSIZE, buffer + i * PGSIZE, size - i * PGSIZE);
        } else {
          pg_mark_dirty(s_fd->mmap->base + i * PGSIZE, PGSIZE);
          memcpy(s_fd->mmap->base + i * PGSIZE, buffer + i * PGSIZE, PGSIZE);
        }
      }
    }
  }

#else
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
#endif
  lock_release(&fileSystemLock);
  // printf("SIZE IS: %d\n", size);
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
    fileDesc->mmap = NULL;

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

// TODO: Fd 0 reads from the keyboard using input_getc().
static int read(uint32_t *args) {

  // printf("Read Called\n");
 int fd = (int) args[1];
 void *buffer = (void *) args[2];
 unsigned length = (unsigned) args[3];
 struct file *fp = NULL;
 int bytes_read = 0;


 //
 // printf("%%ESP: %x\n", thread_current()->stack);
 // printf("Read called...address is: %x\n", buffer);
 //
 // uint32_t *pt = pagedir_get_page(thread_current()->pagedir, buffer);
 //
 // if (*pt & PTE_W == 0) {
 //   printf("RO page\n");
 // }


 if (!is_user_vaddr(buffer) || fd == 1 || fd == 2) {
   exit(NULL);
   thread_exit();
 }
 // if (!isValidAddr(buffer) || fd == 1 || fd == 2) {
 //   printf("Not VAlid\n");
 //   if (is_user_vaddr(buffer)) {
 //     printf("Buffer Now RD: %x\n", pg_round_down(buffer));
 //     printf("Buffer Now:    %x\n", buffer);
 //     if (pagedir_get_page(thread_current()->pagedir,(void *) pg_round_down(buffer)) == NULL) {
 //       printf("No Page Dir\n");
 //     } else {
 //       printf("We have a page dir\n");
 //     }
 //   }
 //   printf("Buffer is: %x\n", buffer);
 //   exit(NULL);
 //   thread_exit();
 // }
 if (fd == 0) {
   unsigned length_copy = length;
   uint32_t* buff = buffer;
   int i = 0;
   while (length-- > 0) {
     buff[i] = input_getc();
     // *((uint32_t)buffer + i++)= input_getc();
     i++;
   }

   return length_copy - length; //should be length
 }
 lock_acquire(&fileSystemLock);
#ifdef VM
  if (fd == 0) {
    unsigned length_copy = length;
    uint32_t* buff = buffer;
    int i = 0;
    while (length-- > 0) {
      buff[i] = input_getc();
      // *((uint32_t)buffer + i++)= input_getc();
      i++;
    }

    return length_copy - length; //should be length
  } else {
    //printf("Before PF\n");
    // Hasnt Faulted Yet
    struct fileDescriptor *s_fd = getFD(fd, thread_current());
    if (s_fd == NULL) {
      lock_release(&fileSystemLock);
      return 0;
    }
    if (s_fd->mmap == NULL) {
      bytes_read = file_read(s_fd->file, buffer, (uint32_t) length);
      //printf("No PF YET\n");
      lock_release(&fileSystemLock);
      return bytes_read;
    } else {
      if (length > (unsigned) s_fd->mmap->file_size) {
        lock_release(&fileSystemLock);
        return 0;
      }

      int i = 0;
      int numPages = DIV_ROUND_UP(length, PGSIZE);
      // memcpy (destination, source, length)
      for (; i < numPages; i++) {
        if (i == numPages - 1) {
          memcpy(buffer + PGSIZE * i, s_fd->mmap->base + i * PGSIZE, length - i * PGSIZE);
        } else {
          memcpy(buffer + PGSIZE * i, s_fd->mmap->base + i * PGSIZE, PGSIZE);
        }
      }
    }
  }

#else
 fp = getFileFromFD(fd, thread_current());
 if (fp == NULL) {
   lock_release(&fileSystemLock);
   return 0;
 }
 bytes_read = file_read(fp, buffer, (uint32_t) length);
#endif
 lock_release(&fileSystemLock);

 // printf("Bytes Read: %d\n", bytes_read);
 return bytes_read;
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


#ifdef VM

static mapid_t mmap(uint32_t *args) {

  int fd = (int) args[1];
  void *addr = (void *) args[2];
  struct fileDescriptor *s_fd = NULL;

  if (!is_v_mmap_addr(addr) || pg_ofs(addr) != 0 || fd == 0 || fd == 1) {
    // printf("address is: %x\n", addr);
    return -1;
  }

  lock_acquire(&fileSystemLock);

  if ((s_fd = getFD(fd, thread_current())) != NULL) {
    if (s_fd->mmap != NULL) {
      lock_release(&fileSystemLock);
      return -1;
    }

    struct mmap_file *_mmapFile = vm_install_mmap(addr, s_fd->file, fd);

    if (_mmapFile == NULL) {
      lock_release(&fileSystemLock);
      return -1;
    }

    lock_acquire(&_mmapLock);
    list_push_back(&_mmapList, &_mmapFile->elem);
    lock_release(&_mmapLock);

    lock_release(&fileSystemLock);
    return _mmapFile->m_id;

  }

  lock_release(&fileSystemLock);
  return -1;
}

static void munmap (uint32_t *args) {

  mapid_t mmap_id = (mapid_t) args[1];
  //int i = 0;

  lock_acquire(&_mmapLock);
  struct mmap_file *_mmapFile = _findMmapFile(mmap_id);
  lock_release(&_mmapLock);

  if (_mmapFile == NULL)
    return;

  if (!vm_muunmap_helper(_mmapFile)) {
    printf("not PANIC() but something went terribly wrong\n");
  }

  lock_acquire(&_mmapLock);
  list_remove(&_mmapFile->elem);
  lock_release(&_mmapLock);

  free(_mmapFile);
}

void mmap_write_back_on_shutdown(void) {

  struct mmap_file *_mmap_file = _find_mmap_by_thread(thread_current());
  while (_mmap_file != NULL) {
    if (!vm_muunmap_helper(_mmap_file))
      printf("not PANIC() but something went terribly wrong\n");

    lock_acquire(&_mmapLock);
    list_remove(&_mmap_file->elem);
    lock_release(&_mmapLock);
    free(_mmap_file);

    _mmap_file = _find_mmap_by_thread(thread_current());
  }
}

#endif

//Helper Functions

#ifdef VM

struct mmap_file *_findMmapFile(mapid_t mid) {

  ASSERT(lock_held_by_current_thread(&_mmapLock));

  struct thread *t = thread_current();
  struct list_elem *iter;

  for (iter = list_begin(&_mmapList); iter != list_end(&_mmapList); iter = list_next(iter)) {
    struct mmap_file *_mmapFile = list_entry(iter, struct mmap_file, elem);
    if (_mmapFile->m_id == mid && _mmapFile->owner == t) {
      return _mmapFile;
    }
  }

  return NULL;
}

struct mmap_file*_find_mmap_by_thread(struct thread *t) {
  ASSERT (t != NULL);
  lock_acquire(&_mmapLock);
  struct list_elem *iter;
  for (iter = list_begin(&_mmapList); iter != list_end(&_mmapList); iter = list_next(iter)) {
    struct mmap_file *_mmap_file = list_entry(iter, struct mmap_file, elem);
    if (_mmap_file->owner == t) {
      lock_release(&_mmapLock);
      return _mmap_file;
    }
  }

  lock_release(&_mmapLock);
  return NULL;
}

int get_mmap_id(void) {
  lock_acquire(&mmap_id_lock);
  int mmap_id = mmapID++;
  lock_release(&mmap_id_lock);
  return mmap_id;
}

#endif

void setReturnStatus(tid_t threadID, int retStatus) {
  struct returnStatus *rs = malloc(sizeof(struct returnStatus));
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

#ifdef VM
static bool is_v_mmap_addr(uint32_t *vaddr) {
  struct thread *cur = thread_current();
  return vaddr != NULL && is_user_vaddr(vaddr) && (pagedir_get_page(cur->pagedir, (void *) vaddr) == NULL);
}
#endif


static bool isValidAddr(uint32_t *vaddr) {

  struct thread *cur = thread_current();

  // if (vaddr == NULL) {
  //   //printf("%d\n", *vaddr);
  //   printf("vaddr is null\n");
  // }
  //
  // if (!is_user_vaddr(vaddr)) {
  //   printf("kernel address\n");
  // }
  //
  // if (!pagedir_get_page(cur->pagedir, (void *) vaddr)) {
  //   printf("Address not mapped in p page dir\n");
  //   printf("%d\n", *vaddr);
  // }
  //check if its a user address
  // int *PHYS_BASE = (int *)0xC0000000;
  // 0xC0000000 > vaddr
  return vaddr != NULL && is_user_vaddr(vaddr) && pagedir_get_page(cur->pagedir,(void *) vaddr);
}

struct file *getFileFromFD(int fd, struct thread *t) {
  //assume lock as been acquired
  struct list_elem *iter;
  for (iter = list_begin(&t->fdList); iter != list_end(&t->fdList); iter = list_next(iter)) {
    struct fileDescriptor *fdStruct = list_entry(iter, struct fileDescriptor, threadFDList);
    if (fdStruct->fd == fd && fdStruct->t == t)
      return fdStruct->file;
  }
  return NULL;
}

struct fileDescriptor *getFD(int fd, struct thread *t) {
  struct list_elem *iter;
  for (iter = list_begin(&t->fdList); iter != list_end(&t->fdList); iter = list_next(iter)) {
    struct fileDescriptor *fdStruct = list_entry(iter, struct fileDescriptor, threadFDList);
    if (fdStruct->fd == fd && fdStruct->t ==t)
      return fdStruct;
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
struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t) {
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

struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t) {
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
