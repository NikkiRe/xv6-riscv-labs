#ifndef KERNEL_DYNAMIC_ARRAY_H
#define KERNEL_DYNAMIC_ARRAY_H

#include "types.h"

// Generic dynamic array of bytes
// data points to contiguous buffer, size is number of used elements,
// capacity is capacity in elements, struct_size is element size.
struct dynamic_array {
  char   *data;
  uint    capacity;
  uint    size;
  int     struct_size;
};

// Initialize array (capacity * struct_size bytes).
int create_dynamic_array(struct dynamic_array *arr, uint capacity, int struct_size);

// Extend capacity to nu_capacity (in elements).
int extend_dynamic_array(struct dynamic_array *arr, uint nu_capacity);

// Shrink capacity (in elements) down to at least arr->size.
// If new_capacity == arr->size it's "shrink_to_fit".
int shrink_dynamic_array(struct dynamic_array *arr, uint new_capacity);

// Convenience: shrink strictly to size (release slack pages if possible).
int shrink_to_fit_dynamic_array(struct dynamic_array *arr);

// Add element (copy struct_size bytes from data).
int push_to_dynamic_array(struct dynamic_array* arr, const char* data);

// Remove last element (size--).
int pop_from_dynamic_array(struct dynamic_array* arr);

// Free buffer (without calling element destructors).
void free_dynamic_array(struct dynamic_array *arr);

#endif // KERNEL_DYNAMIC_ARRAY_H

