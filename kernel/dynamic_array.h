#ifndef KERNEL_DYNAMIC_ARRAY_H
#define KERNEL_DYNAMIC_ARRAY_H

#include "types.h"

// Обобщённый динамический массив байтов (как у тебя):
// data указывает на непрерывный буфер, size — занятых элементов,
// capacity — вместимость в элементах, struct_size — размер элемента.
struct dynamic_array {
  char   *data;
  size_t  capacity;
  size_t  size;
  int     struct_size;
};

// Инициализация массива (capacity * struct_size байт).
int create_dynamic_array(struct dynamic_array *arr, size_t capacity, int struct_size);

// Увеличение capacity до nu_capacity (в элементах).
int extend_dynamic_array(struct dynamic_array *arr, size_t nu_capacity);

// Добавить элемент (копия struct_size байт по адресу data).
int push_to_dynamic_array(struct dynamic_array* arr, const char* data);

// Убрать последний элемент (size--).
int pop_from_dynamic_array(struct dynamic_array* arr);

// Освободить буфер (без вызова деструктора элементов).
void free_dynamic_array(struct dynamic_array *arr);

#endif // KERNEL_DYNAMIC_ARRAY_H

