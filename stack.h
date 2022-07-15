#ifndef STACK_H_INCLUDED
#define STACK_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

/**
 * Type for individual stack entry
 */
struct stack_entry {
  char *data;
  struct stack_entry *next;
};

/**
 * Type for stack instance
 */
struct stack_t
{
  struct stack_entry *head;
  size_t stackSize;  // not strictly necessary, but
                     // useful for logging
};

struct stack_t *newStack(void);

char *copyString(char *str);

void push(struct stack_t *theStack, char *value);

char *top(struct stack_t *theStack);

void pop(struct stack_t *theStack);

void clear_stack(struct stack_t *theStack);

void destroy_stack(struct stack_t **theStack);

#ifdef __cplusplus
}
#endif

#endif /* STACK_H_INCLUDED */
