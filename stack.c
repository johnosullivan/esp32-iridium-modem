/**
 * @file stack.c
 * @brief Implementation of a linked-list based stack for string data
 * @author John O'Sullivan <john@osullivan.dev>
 * @date 2024
 * 
 * This file contains the implementation of the stack data structure declared in stack.h.
 * The implementation uses a singly linked list where new entries are added at the head
 * for O(1) push operations. Memory management is handled carefully to prevent leaks.
 */

#include "stack.h"

/**
 * @brief Creates a new empty stack instance
 * 
 * Allocates memory for a new stack structure and initializes all fields to
 * represent an empty stack. The stack is ready for immediate use after creation.
 * 
 * @return Pointer to the newly created stack, or NULL if memory allocation failed
 * 
 * @note The caller is responsible for freeing the returned stack using destroy_stack()
 * @note This function performs a single malloc() call for the stack structure
 */
struct stack_t *newStack(void)
{
  struct stack_t *stack = malloc(sizeof *stack);
  if (stack)
  {
    stack->head = NULL;          /* Initialize to empty stack */
    stack->stackSize = 0;        /* No entries initially */
  }
  return stack;
}

/**
 * @brief Creates a deep copy of a string
 * 
 * Allocates new memory and copies the input string. This function is used
 * internally by the stack to ensure data ownership and prevent issues with
 * external string modifications or premature deallocation.
 * 
 * @param str Pointer to the source string to copy (must be null-terminated)
 * @return Pointer to the newly allocated copy, or NULL if allocation failed
 * 
 * @note The returned string must be freed by the caller when no longer needed
 * @note This function assumes strdup() is not available, so it manually allocates
 *       and copies the string using malloc() and strcpy()
 * @note The function allocates strlen(str) + 1 bytes to accommodate the null terminator
 */
char *copyString(char *str)
{
  char *tmp = malloc(strlen(str) + 1);  /* +1 for null terminator */
  if (tmp)
    strcpy(tmp, str);
  return tmp;
}

/**
 * @brief Pushes a new string onto the top of the stack
 * 
 * Creates a new stack entry, copies the input string, and adds it to the
 * top of the stack. The operation is O(1) as new entries are added at the head.
 * The stack size is automatically incremented upon successful push.
 * 
 * @param theStack Pointer to the stack to push onto (must not be NULL)
 * @param value Pointer to the string to push (will be copied, must not be NULL)
 * 
 * @note The function makes a copy of the input string, so the original
 *       can be safely modified or freed after the call
 * @note If memory allocation fails, the push operation is silently ignored
 * @note This function performs two malloc() calls: one for the entry structure
 *       and one for the string copy
 */
void push(struct stack_t *theStack, char *value)
{
  struct stack_entry *entry = malloc(sizeof *entry); 
  if (entry)
  {
    entry->data = copyString(value);    /* Create copy of the string */
    entry->next = theStack->head;       /* Link to current head */
    theStack->head = entry;             /* Make new entry the head */
    theStack->stackSize++;              /* Increment size counter */
  }
  else
  {
    /* Memory allocation failed - silently ignore the push operation
     * In a production environment, you might want to log this error
     * or implement a different error handling strategy */
  }
}

/**
 * @brief Returns the string at the top of the stack without removing it
 * 
 * Returns a pointer to the string data at the top of the stack. The entry
 * remains in the stack and can be accessed multiple times. This is an O(1) operation.
 * 
 * @param theStack Pointer to the stack to examine (must not be NULL)
 * @return Pointer to the string at the top, or NULL if stack is empty
 * 
 * @note The returned pointer is valid until the entry is popped or the stack is destroyed
 * @note This function does not modify the stack structure
 * @note Returns NULL if theStack is NULL or if the stack is empty
 */
char *top(struct stack_t *theStack)
{
  if (theStack && theStack->head)
    return theStack->head->data;        /* Return data from top entry */
  else
    return NULL;                        /* Stack is empty or invalid */
}

/**
 * @brief Removes the top entry from the stack
 * 
 * Removes and frees the top entry from the stack, including its string data.
 * The stack size is automatically decremented. If the stack is empty,
 * this function has no effect. This is an O(1) operation.
 * 
 * @param theStack Pointer to the stack to pop from (must not be NULL)
 * 
 * @note This function frees the memory associated with the popped entry
 * @note The function performs two free() calls: one for the string data
 *       and one for the entry structure
 * @note If theStack is NULL, this function has undefined behavior
 */
void pop(struct stack_t *theStack)
{
  if (theStack->head != NULL)
  {
    struct stack_entry *tmp = theStack->head;    /* Save pointer to current head */
    theStack->head = theStack->head->next;       /* Move head to next entry */
    free(tmp->data);                             /* Free the string data */
    free(tmp);                                   /* Free the entry structure */
    theStack->stackSize--;                       /* Decrement size counter */
  }
}

/**
 * @brief Removes all entries from the stack
 * 
 * Iterates through all entries in the stack, freeing the string data and
 * entry structures. The stack is left in an empty state but the stack
 * structure itself remains valid for reuse. This is an O(n) operation.
 * 
 * @param theStack Pointer to the stack to clear (must not be NULL)
 * 
 * @note This function frees all memory associated with stack entries
 * @note The stack structure itself is not freed and can be reused
 * @note If theStack is NULL, this function has undefined behavior
 */
void clear_stack(struct stack_t *theStack)
{
  while (theStack->head != NULL)        /* Continue until stack is empty */
    pop(theStack);                      /* Remove top entry */
}

/**
 * @brief Completely destroys the stack and frees all associated memory
 * 
 * Clears all entries from the stack and then frees the stack structure itself.
 * The stack pointer is set to NULL to prevent use-after-free errors.
 * This is an O(n) operation where n is the number of entries.
 * 
 * @param theStack Pointer to a pointer to the stack to destroy
 * 
 * @note This function frees all memory associated with the stack, including
 *       the stack structure itself. The pointer is set to NULL after destruction.
 * @note If *theStack is NULL, this function has no effect
 * @note The function performs clear_stack() followed by free() on the stack structure
 */
void destroy_stack(struct stack_t **theStack)
{
  clear_stack(*theStack);               /* Remove all entries */
  free(*theStack);                      /* Free the stack structure */
  *theStack = NULL;                     /* Prevent use-after-free */
}