/**
 * @file stack.h
 * @brief A simple linked-list based stack implementation for string data
 * @author John O'Sullivan <john@osullivan.dev>
 * @date 2024
 * 
 * This header file provides a stack data structure implementation using a linked list
 * approach. The stack stores string data and provides standard stack operations:
 * push, pop, top, clear, and destroy. The implementation is thread-safe for basic
 * operations but does not include built-in synchronization.
 * 
 * Usage example:
 * @code
 * struct stack_t *stack = newStack();
 * push(stack, "Hello");
 * push(stack, "World");
 * char *top_item = top(stack);  // Returns "World"
 * pop(stack);
 * destroy_stack(&stack);
 * @endcode
 */

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
 * @brief Individual stack entry structure
 * 
 * Represents a single node in the linked list stack. Each entry contains
 * a pointer to the string data and a pointer to the next entry in the stack.
 */
struct stack_entry {
  char *data;                    /**< Pointer to the string data stored in this entry */
  struct stack_entry *next;      /**< Pointer to the next entry in the stack (NULL if last) */
};

/**
 * @brief Main stack structure
 * 
 * Contains the head pointer to the first entry and maintains a count of
 * the total number of entries for efficient size queries and debugging.
 */
struct stack_t
{
  struct stack_entry *head;      /**< Pointer to the top entry of the stack */
  size_t stackSize;              /**< Current number of entries in the stack */
};

/**
 * @brief Creates a new empty stack
 * 
 * Allocates memory for a new stack structure and initializes it to an empty state.
 * The caller is responsible for freeing the stack using destroy_stack().
 * 
 * @return Pointer to the newly created stack, or NULL if allocation failed
 * 
 * @note This function allocates memory. Use destroy_stack() to free it.
 */
struct stack_t *newStack(void);

/**
 * @brief Creates a deep copy of a string
 * 
 * Allocates new memory and copies the input string. This is used internally
 * by the stack to ensure data ownership and prevent issues with external
 * string modifications.
 * 
 * @param str Pointer to the source string to copy
 * @return Pointer to the newly allocated copy, or NULL if allocation failed
 * 
 * @note The returned string must be freed by the caller when no longer needed
 */
char *copyString(char *str);

/**
 * @brief Pushes a new string onto the top of the stack
 * 
 * Creates a new stack entry containing a copy of the input string and
 * adds it to the top of the stack. The stack size is automatically incremented.
 * 
 * @param theStack Pointer to the stack to push onto
 * @param value Pointer to the string to push (will be copied)
 * 
 * @note The function makes a copy of the input string, so the original
 *       can be safely modified or freed after the call
 */
void push(struct stack_t *theStack, char *value);

/**
 * @brief Returns the string at the top of the stack without removing it
 * 
 * Returns a pointer to the string data at the top of the stack. The entry
 * remains in the stack and can be accessed multiple times.
 * 
 * @param theStack Pointer to the stack to examine
 * @return Pointer to the string at the top, or NULL if stack is empty
 * 
 * @note The returned pointer is valid until the entry is popped or the stack is destroyed
 */
char *top(struct stack_t *theStack);

/**
 * @brief Removes the top entry from the stack
 * 
 * Removes and frees the top entry from the stack, including its string data.
 * The stack size is automatically decremented. If the stack is empty,
 * this function has no effect.
 * 
 * @param theStack Pointer to the stack to pop from
 * 
 * @note This function frees the memory associated with the popped entry
 */
void pop(struct stack_t *theStack);

/**
 * @brief Removes all entries from the stack
 * 
 * Iterates through all entries in the stack, freeing the string data and
 * entry structures. The stack is left in an empty state but the stack
 * structure itself remains valid for reuse.
 * 
 * @param theStack Pointer to the stack to clear
 * 
 * @note This function frees all memory associated with stack entries
 */
void clear_stack(struct stack_t *theStack);

/**
 * @brief Completely destroys the stack and frees all associated memory
 * 
 * Clears all entries from the stack and then frees the stack structure itself.
 * The stack pointer is set to NULL to prevent use-after-free errors.
 * 
 * @param theStack Pointer to a pointer to the stack to destroy
 * 
 * @note This function frees all memory associated with the stack, including
 *       the stack structure itself. The pointer is set to NULL after destruction.
 */
void destroy_stack(struct stack_t **theStack);

#ifdef __cplusplus
}
#endif

#endif /* STACK_H_INCLUDED */
