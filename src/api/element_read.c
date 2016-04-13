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

#include "hio_internal.h"

ssize_t hio_element_read (hio_element_t element, off_t offset, unsigned long reserved0, void *ptr,
                          size_t count, size_t size) {
  return hio_element_read_strided (element, offset, reserved0, ptr, count, size, 0);
}

int hio_element_read_nb (hio_element_t element, hio_request_t *request, off_t offset,
                         unsigned long reserved0, void *ptr, size_t count, size_t size) {
  return hio_element_read_strided_nb (element, request, offset, reserved0, ptr, count, size, 0);
}

ssize_t hio_element_read_strided (hio_element_t element, off_t offset, unsigned long reserved0, void *ptr,
                                  size_t count, size_t size, size_t stride) {
  hio_request_t request = NULL;
  ssize_t bytes_transferred;
  int rc;

  rc = hio_element_read_strided_nb (element, &request, offset, reserved0, ptr, count, size, stride);
  if (HIO_SUCCESS != rc && NULL == request) {
      return rc;
  }

  hio_request_wait (&request, 1, &bytes_transferred);

  return bytes_transferred;
}

int hio_element_read_strided_nb (hio_element_t element, hio_request_t *request, off_t offset,
                                 unsigned long reserved0, void *ptr, size_t count, size_t size,
                                 size_t stride) {
  hio_dataset_t dataset = hioi_element_dataset (element);

  if (HIO_OBJECT_NULL == element || offset < 0) {
    return HIO_ERR_BAD_PARAM;
  }

  (void) atomic_fetch_add (&dataset->ds_stat.s_rcount, 1);

  int rc = element->e_read_strided_nb (element, request, offset, ptr, count, size, stride);
  if (HIO_SUCCESS != rc && (NULL == request || NULL == *request)) {
      return rc;
  }

  return rc;
}
