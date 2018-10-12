#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "filesys/off_t.h"

void syscall_init (void);

//helpers
// struct file *getFileFromFD(int fd, struct thread *);
// //struct fileDescriptor *closeHelper(int fd, struct list *, bool, struct thread *);
// struct fileDescriptor *closeHelperThread(int fd, struct list *lst, struct thread *t);
// struct fileDescriptor *closeHelperGlobal(int fd, struct list *lst, struct thread *t);
// bool isValidAddr(uint32_t *);

//core syscall
int write(uint32_t *args);
//int open(uint32_t *args);
//void close(uint32_t *args);
void exit(uint32_t *args);
// unsigned tell (uint32_t *);
// int filesize (uint32_t *);
// void halt(void);


#endif /* userprog/syscall.h */


/*
enum    {
  SYS_HALT,
  SYS_EXIT,
  SYS_EXEC,
  SYS_WAIT,
  SYS_CREATE,
  SYS_REMOVE,
  SYS_OPEN,
  SYS_FILESIZE,
  SYS_READ,
  SYS_WRITE,
  SYS_SEEK,
  SYS_TELL,
  SYS_CLOSE,
  };
*/
