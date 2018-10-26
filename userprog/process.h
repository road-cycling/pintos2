#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
