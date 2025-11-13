#include "types.h"
#include "param.h"
#include "riscv.h"
#include "dynamic_array.h"
#include "defs.h"

// Safe multiplication of two uint values.
// Returns 0 on success, -1 on overflow.
static inline int
mul_overflow_uint(uint a, uint b, uint *out)
{
  if (out == 0) return -1;
  if (a == 0 || b == 0) { *out = 0; return 0; }
  uint x = a * b;
  if (x / a != b) return -1;
  *out = x;
  return 0;
}

// Zero tail elements [from, to) (safely calculates bytes).
static inline void
da_bzero_tail(struct dynamic_array *arr, uint from, uint to)
{
  if (arr == 0 || arr->data == 0 || arr->struct_size <= 0) return;
  if (to <= from) return;

  uint off_bytes = 0, len_elems = to - from, len_bytes = 0;
  if (mul_overflow_uint(from, (uint)arr->struct_size, &off_bytes) < 0) return;
  if (mul_overflow_uint(len_elems, (uint)arr->struct_size, &len_bytes) < 0) return;

  memset(arr->data + off_bytes, 0, len_bytes);
}

// Initialize array (capacity * struct_size bytes).
int
create_dynamic_array(struct dynamic_array *arr, uint capacity, int struct_size)
{
  if (arr == 0 || struct_size <= 0)
    return -1;

  arr->data = 0;
  arr->capacity = 0;
  arr->size = 0;
  arr->struct_size = struct_size;

  if (capacity == 0)
    return 0;

  uint bytes = 0;
  if (mul_overflow_uint(capacity, (uint)struct_size, &bytes) < 0)
    return -1;

  arr->data = (char*)bd_malloc(bytes);
  if (arr->data == 0)
    return -1;

  arr->capacity = capacity;
  return 0;
}

// Extend capacity to nu_capacity (in elements).
int
extend_dynamic_array(struct dynamic_array *arr, uint nu_capacity)
{
  if (arr == 0)
    return -1;
  if (nu_capacity <= arr->capacity)
    return 0;

  uint bytes = 0;
  if (mul_overflow_uint(nu_capacity, (uint)arr->struct_size, &bytes) < 0)
    return -1;

  char *new_data = (char*)bd_malloc(bytes);
  if (new_data == 0)
    return -1;

  if (arr->size > 0 && arr->data != 0) {
    uint copy_bytes = 0;
    if (mul_overflow_uint(arr->size, (uint)arr->struct_size, &copy_bytes) < 0) {
      bd_free(new_data);
      return -1;
    }
    memmove(new_data, arr->data, copy_bytes);
    bd_free(arr->data);
  }

  arr->data = new_data;
  arr->capacity = nu_capacity;
  return 0;
}

// Shrink capacity (in elements) down to at least arr->size.
// If new_capacity == arr->size it's "shrink_to_fit".
int
shrink_dynamic_array(struct dynamic_array *arr, uint new_capacity)
{
  if (arr == 0)
    return -1;

  if (arr->size == 0) {
    if (arr->data) {
      bd_free(arr->data);
      arr->data = 0;
    }
    arr->capacity = 0;
    return 0;
  }

  if (new_capacity < arr->size)
    new_capacity = arr->size;

  if (new_capacity >= arr->capacity)
    return 0;

  uint bytes = 0;
  if (mul_overflow_uint(new_capacity, (uint)arr->struct_size, &bytes) < 0)
    return -1;

  char *new_data = (char*)bd_malloc(bytes);
  if (new_data == 0)
    return -1;

  uint copy_bytes = 0;
  if (mul_overflow_uint(arr->size, (uint)arr->struct_size, &copy_bytes) < 0) {
    bd_free(new_data);
    return -1;
  }

  memmove(new_data, arr->data, copy_bytes);
  bd_free(arr->data);

  arr->data = new_data;
  arr->capacity = new_capacity;
  return 0;
}

// Shrink strictly to size (release slack pages if possible).
int
shrink_to_fit_dynamic_array(struct dynamic_array *arr)
{
  if (arr == 0)
    return -1;
  return shrink_dynamic_array(arr, arr->size);
}

// Add element (copies struct_size bytes from data).
int
push_to_dynamic_array(struct dynamic_array* arr, const char* data)
{
  if (arr == 0 || data == 0 || arr->struct_size <= 0)
    return -1;

  if (arr->capacity == 0) {
    if (extend_dynamic_array(arr, 8) != 0)
      return -1;
  }

  if (arr->size >= arr->capacity) {
    uint nu_capacity = (arr->capacity < 8) ? 8 : (arr->capacity * 2);
    if (extend_dynamic_array(arr, nu_capacity) != 0)
      return -1;
  }

  uint off_bytes = 0;
  if (mul_overflow_uint(arr->size, (uint)arr->struct_size, &off_bytes) < 0)
    return -1;

  memmove(arr->data + off_bytes, data, (uint)arr->struct_size);
  arr->size++;
  return 0;
}

// Remove last element.
// Policy: free buffer when size==0; shrink capacity by half when utilization <= 25%.
int
pop_from_dynamic_array(struct dynamic_array* arr)
{
  if (arr == 0 || arr->size == 0)
    return -1;

  uint new_size = arr->size - 1;

  da_bzero_tail(arr, new_size, new_size + 1);

  arr->size = new_size;

  if (arr->size == 0) {
    if (arr->data) {
      bd_free(arr->data);
      arr->data = 0;
    }
    arr->capacity = 0;
    return 0;
  }

  if (arr->capacity >= 8 && arr->size <= (arr->capacity >> 2)) {
    uint new_capacity = arr->capacity >> 1;
    if (new_capacity < arr->size)
      new_capacity = arr->size;
    (void)shrink_dynamic_array(arr, new_capacity);
  }

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
