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
#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"


#ifdef VM
#include "lib/round.h"
#include "vm/frame.h"
#endif

typedef int pid_t;

struct lock fileSystemLock;
static struct list FD;
static struct list returnStatusStruct;

// Linux Kernel Implementation
// struct perf_mmap {
// 	void		 *base;
// 	int		 mask;
// 	int		 fd;
// 	int		 cpu;
// 	refcount_t	 refcnt;
// 	u64		 prev;
// 	u64		 start;
// 	u64		 end;
// 	bool		 overwrite;
// 	struct auxtrace_mmap auxtrace_mmap;
// 	char		 event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
// };

#ifdef VM
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
// static mapid_t mmap(uint32_t *args);
// static void muunmap (uint32_t *args);
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

  // f->eax = syscall_ptr[*args](args);
    //f->eax = (*syscall_ptr[1])();
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
  } //else if (*args == SYS_MMAP) {
  //   f->eax = mmap(args);
  // } else if (*args == SYS_MUNMAP) {
  //   muunmap(args);
  // }
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

  // int returnStatus = args == NULL ? -1 :
  //                       isValidAddr((void *) args[1]) ?
  //                       (int) *(args + 1) :
  //                       -1;
  //int returnStatus = isValidAddr((void *) args[1]) ? (int) *(args + 1) : -1;
  //int returnStatus = 0;

  // if (isValidAddr((void *) args[1])) {
  //   printf("Address is valid\n");
  //   returnStatus = (int) *(args + 1);
  // } else {
  //   printf("Address is not valid");
  //   returnStatus = -1;
  // }



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
    //fileDesc->mmap = NULL;

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

/*

Your VM system must lazily load pages in mmap regions and use the mmaped file itself
as backing store for the mapping. That is, evicting a page mapped by mmap writes it
back to the file it was mapped from.
If the file’s length is not a multiple of PGSIZE, then some bytes in the final mapped
page “stick out” beyond the end of the file. Set these bytes to zero when the page is
faulted in from the file system, and discard them when the page is written back to
disk.
If successful, this function returns a “mapping ID” that uniquely identifies the mapping
within the process. On failure, it must return -1, which otherwise should not be
a valid mapping id, and the process’s mappings must be unchanged.
A call to mmap may fail if the file open as fd has a length of zero bytes. It must fail
if addr is not page-aligned or if the range of pages mapped overlaps any existing set
of mapped pages, including the stack or pages mapped at executable load time. It
must also fail if addr is 0, because some Pintos code assumes virtual page 0 is not
mapped. Finally, file descriptors 0 and 1, representing console input and output, are
not mappable.
*/

#ifdef VM
//
// static mapid_t mmap(uint32_t *args) {
//   int fd = (int) args[1];
//   void *addr = (void *) args[2];
//   struct file *fp = NULL;
//
//   // @reopen file?
//   if (!isValidAddr(addr) || pg_ofs(addr) != 0 || fd == 0 || fd == 1) {
//     exit(NULL);
//     thread_exit();
//   }
//
//   lock_acquire(&fileSystemLock);
//   if ((fp = getFileFromFD(fd, thread_current())) != NULL) {
//     fp = file_reopen(fp);
//
//     uint32_t size_file_bytes = file_length(fp);
//     int pages_taken = DIV_ROUND_UP(size_file_bytes, PGSIZE);
//     struct thread *t = thread_current();
//
//     struct mmap_file *_mmapFile = malloc(sizeof(struct mmap_file));
//     _mmapFile->base = addr;
//     _mmapFile->fd = fd;
//     _mmapFile->m_id = mmapID++; /* bad */
//     _mmapFile->owner = thread_current();
//     _mmapFile->pages_taken = pages_taken;
//
//     size_t file_ofs = 0;
//
//     int i = 0;
//     struct sPageTAbleEntry *spte = NULL;
//     for (; i < pages_taken; i++) {
//       spte = page_lookup(addr + PGSIZE * i, &t->s_pte);
//
//       if (spte != NULL) {
//         //release SPTE entries created so far
//         lock_release(&fileSystemLock);
//         return -1
//       }
//
//       spte = getCustomSupPTE(addr + PGSIZE * i, LOC_MMAP, fp, file_ofs, 0);
//       hash_insert(&spte->hash_elem, &t->s_pte);
//
//       file_ofs += PGSIZE;
//     }
//
//     // while (size_file_bytes > 0) {
//     //
//     //   size_t page_read_bytes = size_file_bytes < PGSIZE ? size_file_bytes : PGSIZE;
//     //   size_t page_zero_bytes = PGSIZE - page_read_bytes;
//     //
//     //   uint8_t *kpage = vm_get_frame(PAL_USER);
//     //
//     //   if (file_read_at(fp, kpage, page_read_bytes, file_ofs) != (int) page_read_bytes) {
//     //     // TODO: add logic to free previous frames if alloc.
//     //     free(_mmapFile);
//     //     vm_free_frame(kpage, true);
//     //     lock_release(&fileSystemLock);
//     //     return -1;
//     //   }
//     //
//     //   memset(kpage + page_read_bytes, 0, page_zero_bytes);
//     //
//     //   size_file_bytes -= page_read_bytes;
//     //   file_ofs += page_read_bytes;
//     //   addr += PGSIZE;
//     // }
//     //Finished writing to frames
//
//     // void list_push_back (struct list *, struct list_elem *);
//     lock_acquire(&_mmapLock);
//     list_push_back(&_mmapList, &_mmapFile->elem);
//     lock_release(&_mmapLock);
//
//     lock_release(&fileSystemLock);
//     return _mmapFile->m_id;
//
//   }
//   lock_release(&fileSystemLock);
//
//   return -1;
// }
//
// static void muunmap (uint32_t *args) {
//   mapid_t mmap_id = (mapid_t) args[1];
//   int i = 0;
//
//   lock_acquire(&_mmapLock);
//   struct mmap_file *_mmapFile = _findMmapFile(mmap_id);
//   lock_release(&_mmapLock);
//
//   if (_mmapFile == NULL)
//     return;
//
//   for (; i < _mmapFile->pages_taken; i++) {
//     vm_free_frame(_mmapFile->base + PGSIZE * i, true);
//   }
//
//   lock_acquire(&_mmapLock);
//   list_remove(&_mmapFile->elem);
//   lock_release(&_mmapLock);
//
//   free(_mmapFile);
// }

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
