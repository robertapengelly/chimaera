/******************************************************************************
 * @file            pipe.h
 *****************************************************************************/
#ifndef     _PIPE_H
#define     _PIPE_H

#include    <corazon/tasking/process.h>

int kpipe (uintptr_t address);
void kpipe_update (struct thread *thread);

#endif      /* _PIPE_H */
