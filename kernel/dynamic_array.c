#include "types.h"
#include "riscv.h"
#include "dynamic_array.h"
#include "defs.h"

// Initialize array (capacity * struct_size bytes).
int
create_dynamic_array(struct dynamic_array *arr, uint capacity, int struct_size)
{
  if (arr == 0 || capacity == 0 || struct_size <= 0)
    return -1;

  arr->data = (char*)bd_malloc(capacity * struct_size);
  if (arr->data == 0)
    return -1;

  arr->capacity = capacity;
  arr->size = 0;
  arr->struct_size = struct_size;
  return 0;
}

// Extend capacity to nu_capacity (in elements).
int
extend_dynamic_array(struct dynamic_array *arr, uint nu_capacity)
{
  if (arr == 0 || nu_capacity <= arr->capacity)
    return -1;

  char *new_data = (char*)bd_malloc(nu_capacity * arr->struct_size);
  if (new_data == 0)
    return -1;

  // Copy existing data
  if (arr->size > 0 && arr->data != 0) {
    memmove(new_data, arr->data, arr->size * arr->struct_size);
    bd_free(arr->data);
  }

  arr->data = new_data;
  arr->capacity = nu_capacity;
  return 0;
}

// Add element (copy struct_size bytes from data).
int
push_to_dynamic_array(struct dynamic_array* arr, const char* data)
{
  if (arr == 0 || data == 0)
    return -1;

  // If need to expand array
  if (arr->size >= arr->capacity) {
    uint nu_capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
    if (extend_dynamic_array(arr, nu_capacity) != 0)
      return -1;
  }

  // Copy element to end of array
  memmove(arr->data + arr->size * arr->struct_size, data, arr->struct_size);
  arr->size++;
  return 0;
}

// Remove last element (size--).
int
pop_from_dynamic_array(struct dynamic_array* arr)
{
  if (arr == 0 || arr->size == 0)
    return -1;

  arr->size--;
  return 0;
}

// Free buffer (without calling element destructors).
void
free_dynamic_array(struct dynamic_array *arr)
{
  if (arr == 0)
    return;

  if (arr->data != 0) {
    bd_free(arr->data);
    arr->data = 0;
  }
  arr->capacity = 0;
  arr->size = 0;
  arr->struct_size = 0;
}

