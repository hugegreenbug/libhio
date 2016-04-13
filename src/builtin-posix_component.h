/* -*- Mode: C; c-basic-offset:2 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2014-2016 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#if !defined(BUILTIN_POSIX_COMPONENT_H)
#define BUILTIN_POSIX_COMPONENT_H

#include "hio_internal.h"
#include "hio_component.h"

#define HIO_POSIX_MAX_OPEN_FILES  32

typedef struct builtin_posix_file_t {
  /** file handle */
  hio_file_t f_file;
  /** hio element */
  hio_element_t f_element;
  /** file block id */
  int f_bid;
} builtin_posix_file_t;

/* data types */
typedef struct builtin_posix_module_t {
  hio_module_t base;
  mode_t access_mode;
} builtin_posix_module_t;

typedef struct builtin_posix_module_dataset_t {
  struct hio_dataset base;
  pthread_mutex_t lock;
  builtin_posix_file_t files[HIO_POSIX_MAX_OPEN_FILES];
  char *base_path;
  uint64_t reserved_offset;
  uint64_t reserved_remaining;
} builtin_posix_module_dataset_t;

extern hio_component_t builtin_posix_component;

#endif /* BUILTIN_POSIX_COMPONENT_H */
