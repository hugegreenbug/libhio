/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2014      Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

/**
 * @file hio_api.h
 * @brief API for libhio
 */

/** @ingroup design
 * libHIO is a library for writing data into a hierarchal data store. The data
 * store may contain a single level or multiple levels including a parallel
 * file system (PFS) and burst buffer (BB). The goal of hio is to provide a
 * general IO interface that:
 *
 * - Is always thread-safe. No locking is required outside of libhio.
 *
 * - Provides a simple interface that provides at a minimum posix-like IO
 *   semantics. The semantics will be weakened to allow for optimization within
 *   libhio.
 *
 * - Allows for easy expansion to support new IO models as well as higher level
 *   IO models and languages.
 *
 * - Allows for specification of filesystem specific "hints" while providing
 *   good defaults.
 *
 * - Provides support for multiple backends that can be selected at runtime.
 *   Ideally, binary-only backends can be provided by vendors to support
 *   specific hardware.
 *
 * - Provide easy access to configuration options via environment, api, and
 *   file parsing.
 *
 * - Supports transparent fallback on other IO mechanisms if one fails. An
 *   example is falling back on a paralled filesystem if a burst-buffer
 *   fails.
 *
 * - Provides support for turnstiled IO and n -> 1, n -> n, and n -> m
 *   IO layouts.
 */

#if !defined(HIO_H)
#define HIO_H

/** @ingroup API
 * @brief HIO library context
 *
 * HIO contexts are used to identify specific instances of the hio library.
 * Instances are created with hio_init() and destroyed with hio_finalize().
 * It is possible to have multiple active contexts at any time during
 * execution. This type is opaque.
 */
typedef struct hio_context_t *hio_context_t;

/** @ingroup API
 * @brief HIO data set handle
 *
 * HIO data sets represent one or more HIO "files" on the data store. 
 */
typedef struct hio_set_t *hio_set_t;

/** @ingroup API
 * @brief HIO "file" handle
 *
 * HIO files are the primary mechanism for performing I/O using libhio. A
 * libhio file may or may not correspond to a specific file on a traditional
 * POSIX filesystem. This type is opaque.
 */
typedef struct hio_file_t *hio_file_t;

/** @ingroup API
 * @brief HIO file request
 *
 * HIO requests are the primary mechanism for checking for completion of
 * non-blocking I/O requests. This type is opaque.
 */
typedef struct hio_request_t *hio_request_t;

/** @ingroup API
 * The libhio interface presents a posix-like API for performing hierarchal
 * I/O.
 */

enum hio_error_t {
    /** The hio operation completed successfully */
    HIO_SUCCESS,
    /** Generic hio error */
    HIO_ERROR,
    /** Permissions error */
    HIO_ERR_PERM,
};

/** @ingroup API
 * @brief File open flags
 */
typedef enum hio_flags_t {
    /** Open the file read-only */
    HIO_FLAG_RDONLY   = 0,
    /** Open the file write-only */
    HIO_FLAG_WRONLY   = 1,
    /** Open the file for reading and writing */
    HIO_FLAG_RDWR     = 2,
    /** Create a new file */
    HIO_FLAG_CREAT    = 64,
    /** Truncate the file */
    HIO_FLAG_TRUNC    = 512,
    /** Append instead of truncating the file */
    HIO_FLAG_APPEND   = 1024,
    /**
     * NTH: There are two possible semantics in mind for this flag:
     *   - Do not block waiting for the file to open. This will require
     *     further development on the hio_open interface.
     *   - All operations on the file are non-blocking. In this case
     *     the user would be required to call hio_flush/hio_complete
     *     to ensure operations on the file are complete.
     */
    HIO_FLAG_NONBLOCK = 2048,
    /** The file should be constructed at close */
    HIO_FLAG_CONSTRUCT = 4096,
} hio_flags_t;

/** @ingroup API
 * @brief Flush modes
 */
typedef enum hio_flush_mode_t {
    /** Locally flush data. This mode ensures that the user buffers can
     * be reused by the application. It does not ensure the data has
     * been written out to the backing store. */
    HIO_FLUSH_MODE_LOCAL    = 0,
    /** Ensure all data has been written out to the backing store. */
    HIO_FLUSH_MODE_COMPLETE = 1,
} hio_flush_mode_t;

/** @ingroup API
 * @brief Create a new hio context
 *
 * @param[out] new_context  Newly created hio context
 * @param[in]  name         Context name
 *
 * @returns hio_error_t
 *
 * This function creates a new hio context and returns it
 * to the caller. This function is collective on the
 * process. The meaning of this is yet to be fully
 * defined.
 */
int hio_init (hio_context_t *new_context, char *name);

/** @ingroup API
 * @brief Create a new hio context
 *
 * @param[out] new_context  Newly created hio context
 * @param[in]  comm         MPI communicator
 * @param[in]  name         Context name
 *
 * @returns hio_error_t
 *
 * This function creates a new hio context and returns it
 * to the caller. This function is collective on the
 * process. The meaning of this is yet to be fully
 * defined. The communicator specified in {comm} should
 * contain all the processes that will perform IO in
 * this context.
 */
int hio_init_mpi (hio_context_t *new_context, MPI_Comm *comm, char *name);

/** @ingroup API
 * @brief Set a hint on an HIO object
 *#
 * @param[in] hio_obj HIO object to set hint on
 * @param[in] hint    Hint to set on the hio file
 * @param[in] value   Value to set for the hio hint
 *
 * This function can be used to give hints on how an hio object
 * will be used. Additionally, if the hint starts with file: then
 * the hint will apply to all files in the context opened after the
 * call to hio_object_set_hint.
 *
 * NTH: It will be helpful to have a function that lists the available
 * hints. This way the user can check what is supported with a given
 * configuration.
 *
 * NTH: This function may change as the API develops but it is likely
 * little will change other than the types of the hint and value
 * parameters. For now these parameters are set as character strings
 * to allow us to provide an interface that will adapt to any type of
 * hint. Note: it might also be useful to provide API functions to get
 * the currently set hints or query all possible hints on a file.
 */
int hio_object_set_hint (hio_object_t hio_obj, char *hint, char *value);

/** @ingroup error_handling
 * @brief Get a string representation of the last error
 *
 * @param[in]  ctx    hio context
 * @param[out] error  string representation of the last error
 *
 * This function gets a string representation of the last
 * error that occurred in a given context. Note that if
 * another thread is working in this context the error
 * may be overwritten if another error occurs. It is up
 * to the caller to free the string once they are done
 * with it.
 */
int hio_err_get_last (hio_context_t ctx, char **error);

/** @ingroup error_handling
 * @brief Print 
 *
 * @param[in]  ctx    hio context
 * @param[in]  output Output file handle
 * @param[in]  format Print format (see printf)
 * @param[in]  ...    Format values
 *
 * This funtion prints the user's error string followed by
 * a string representation of the HIO error last seen on
 * this thread.
 */
int hio_err_print_last (hio_context_t ctx, FILE *output, char *format, ...);

/** @ingroup API
 * @brief Finalize an hio context
 *
 * @param[in,out] ctx hio context to finalize
 *
 * @returns hio_error_t
 *
 * This function frees all memory associated with an hio
 * context and finalizes it. It is erroneous to call this function
 * with any outstanding I/O requests (NTH: is this ok, we could
 * block until all I/O is complete, times out, or fails).
 */
int hio_fini (hio_context_t *ctx);

/** @ingroup API
 * @brief Create a data set
 *
 * @param[in]  ctx      hio context
 * @param[out] set_out  HIO data set reference
 * @param[in]  name     Name of HIO data set
 * @param[in]  flags    Open flags
 * @param[in]  set_id   Identifier for this set
 *
 * @returns hio_error_t
 *
 * This function attempts to open an HIO data set. A data set
 * represents a collection of related files. An example is a
 * set of files associated with an n-n IO pattern.
 */
int hio_set_open (hio_context_t ctx, hio_set_t *set_out, const char *name, int64_t set_id,
                  hio_flags_t flags);

/** @ingroup API
 * @brief Close an HIO data set
 *
 * @param[in,out] hio_set  IO data set handle
 *
 * @returns hio_error_t
 *
 * This function closes an HIO data set and releases all resources
 * assocated with the data set.
 */
int hio_set_close (hio_set_t *hio_set);

/** @ingroup API
 * @brief Unlink an hio set
 *
 * @param[in] ctx    hio context
 * @param[in] name   name of HIO data set
 * @param[in] set_id identified for the dataset
 *
 * @returns hio_error_t
 *
 * This function removes all data associated with an HIO data set. 
 */
int hio_set_unlink (hio_context_t ctx, const char *name, int64_t set_id);

/** @ingroup API
 * @brief Open a file (non-blocking)
 *
 * @param[in]  ctx      HIO context
 * @param[in]  set      HIO set this file belongs to
 * @param[out] request  New open request
 * @param[out] file_out New hio file handle
 * @param[in]  filename File to open
 * @param[in]  flags    Open flags
 * @param[in]  ...      (optional) Access mode of the file. Valid
 *                      for HIO_FLAG_CREAT only.
 *
 * @returns hio_error_t
 *
 * This function attempts to open an HIO file. An HIO file may
 * be represented by a single or many files depending on the
 * job size, number of writers, and configuration. The file
 * handle can not be used until the open is complete. This is
 * indicated by completion of the request. NTH: there are other
 * semantics we could offer. We could always assume open is non-blocking
 * and defer all operations until the file is actually open. In the
 * case of read this could mean hio_read blocks until the open request
 * is complete.
 *
 * Comment: This may not be a necessary function for expressing the
 * necessary semantics libhio aims to expose. The "blocking" function
 * could be defined to open the file lazily which would give similar
 * semantics to a non-blocking open.
 */
int hio_open_nb (hio_context_t ctx, hio_set_t set, hio_request_t *request, hio_file_t *file_out, char *filename,
                 hio_flags_t flags, ...);

/** @ingroup API
 * @brief Construct a traditional file out of an HIO file context
 *
 * @param[in] hio_file     hio file handle
 * @param[in] destination  destination file or directory name
 * @param[in] flags        contruct flags
 *
 * This function takes an hio file and reconstructs the file(s) that
 * make up the hio file. This could be a single file or many depending
 * on how the hio file was constructed. This function will fail if
 * the output is a directory and a file exists with the destination
 * name.
 */
int hio_file_construct (hio_file_t hio_file, const char *destination, int flags);


/** @ingroup API
 * @brief Close an open file
 *
 * @param[in,out] hio_file Open hio file handle
 *
 * @returns hio_error_t
 *
 * This function finalizes all outstanding I/O on an open file. On
 * success {hio_file} is set to HIO_FILE_NULL.
 */
int hio_close (hio_file_t *hio_file);


/** @ingroup API
 * @brief Start a non-blocking contiguous write to an HIO file
 *
 * @param[in]  hio_file hio file handle
 * @param[out] request  new hio request (may be NULL)
 * @param[in]  offset   Offset to write to
 * @param[in]  whence   Location the offset is from
 * @param[in]  ptr      Data to write
 * @param[in]  count    Number of elements to write
 * @param[in]  size     Size of each element
 *
 * @returns hio_error_t
 *
 * This function writes data from ptr to the file specified by hio_file.
 * The call returns immediately regardless if the write is complete. The user
 * can call hio_test, hio_wait, and hio_waitall to test and wait for completion.
 * The implementation is free to return HIO_REQUEST_NULL if the write is complete.
 * In the context of writes a request is complete when the buffer specified by
 * {ptr} is free to be modified. Completion of a write request does not garauntee
 * the data has been written.
 */
int hio_write_nb (hio_file_t hio_file, hio_request_t *request, off_t offset, int whence,
                  void *ptr, size_t count, size_t size);

/** @ingroup API
 * @brief Start a non-blocking strided write to an HIO file
 *
 * @param[in]  hio_file hio file handle
 * @param[out] request  new hio request (may be NULL)
 * @param[in]  offset   Offset to write to
 * @param[in]  whence   Location the offset is from
 * @param[in]  ptr      Data to write
 * @param[in]  count    Number of elements to write
 * @param[in]  size     Size of each element
 * @param[in]  stride   Stride between each element
 *
 * @returns hio_error_t
 *
 * This function writes strided data from ptr to the file specified by hio_file.
 * The call returns immediately regardless if the write is complete. The user can call
 * hio_test, hio_wait, and hio_waitall to test and wait for completion. The implementation
 * is free to return HIO_REQUEST_NULL if the write is complete. In the context of writes
 * a request is complete when the buffer specified by {ptr} is free to be modified.
 * Completion of a write request does not garauntee the data has been written.
 */
int hio_write_strided_nb (hio_file_t hio_file, hio_request_t *request, off_t offset, int whence,
                          void *ptr, size_t count, size_t size, size_t stride);

/** @ingroup API
 * @brief Flush all pending writes on a file out to the backing store
 *
 * @param[in] hio_file  HIO file handle
 * @param[in] mode      Flush mode
 *
 * @returns hio_error_t
 *
 * This functions completes all outstanding writes to the specified file.
 * If {hio_file} is HIO_FILE_ALL then all outstanding writes are completed on
 * all open files. The caller can use {mode} to specify whether local completion
 * (flush data from user buffers) or remote completion (data written to backing
 * stor) is desired.
 */
int hio_flush (hio_file_t hio_file, hio_flush_mode_t mode);

/** @ingroup API
 * @brief Start a non-blocking contiguous read from an HIO file
 *
 * @param[in]  hio_file hio file handle
 * @param[out] request  new hio request (may be NULL)
 * @param[in]  offset   Offset to read from
 * @param[in]  whence   Location the offset is from
 * @param[in]  ptr      Buffer to read data into
 * @param[in]  count    Number of elements to read
 * @param[in]  size     Size of each element
 *
 * @returns hio_error_t
 *
 * This function reads data from file specified by hio_file to the
 * buffer specified in ptr. This call returns immediately regardless if
 * the read has completed. The user can call hio_test, hio_wait, and
 * hio_waitall to test and wait for completion. The implementation is free
 * to return HIO_REQUEST_NULL if the read is complete. In the context of reads
 * a request is complete if the requested data is visible in the buffer
 * specified by {ptr}.
 */
int hio_read_nb (hio_file_t hio_file, hio_request_t *request, off_t offset, int whence,
                 void *ptr, size_t count, size_t size);

/** @ingroup API
 * @brief Start a non-blocking strided read
 *
 * @param[in]  hio_file hio file handle
 * @param[out] request  new hio request (may be NULL)
 * @param[in]  offset   Offset to read from
 * @param[in]  whence   Location the offset is from
 * @param[in]  ptr      Buffer to read data into
 * @param[in]  count    Number of elements to read
 * @param[in]  size     Size of each element
 * @param[in]  stride   Stride between each element
 *
 * @returns hio_error_t
 *
 * This function reads strided data from file specified by hio_file to the
 * buffer specified in ptr. This call returns immediately regardless if
 * the read has completed. The user can call hio_test, hio_wait, and
 * hio_waitall to test and wait for completion. The implementation is free
 * to return HIO_REQUEST_NULL if the read is complete. In the context of reads
 * a request is complete if the requested data is visible in the buffer
 * specified by {ptr}.
 */
int hio_read_strided_nb (hio_file_t hio_file, hio_request_t *request, off_t offset,
                         int whence, void *ptr, size_t count, size_t size, size_t stride);

/** @ingroup API
 * @brief Complete all outstanding read operations on a file.
 *
 * @param[in] hio_file hio file handle
 *
 * @returns hio_error_t
 *
 * This functions completes all outstanding reads on a file. If the
 * file handle is HIO_FILE_HANDLE_ALL then all reads on all open files
 * are completed by this call.
 */
int hio_complete (hio_file_t hio_file);

/** @ingroup API
 * @brief Join multiple requests into a single request object
 * 
 * @param[in,out] requests hio requests
 * @param[in]    count    length of {requests}
 * @param[out]   request  combined request
 *
 * This functions takes multiple requests and creates a single request object.
 * The new request is complete when all the joined requests are complete.
 */
int hio_request_join (hio_request_t *requests, int count, hio_request_t *request);

/** @ingroup API
 * @brief Test for completion of an I/O request
 *
 * @param[in,out] request  hio I/O request
 * @param[out]   complete flag indicating whether the request is complete.
 *
 * This function tests for the completion of an I/O request. If the
 * request is complete then {complete} is set to true and the request is
 * released and set to HIO_FILE_NULL.
 */
int hio_test (hio_request_t *request, bool *complete);

/** @ingroup API
 * @brief Wait for completion of an I/O request
 *
 * @param[in,out] request  hio I/O request
 *
 * This function waits for the completion of an I/O request. Once the
 * request is complete this function release {request} and sets it to
 * HIO_FILE_NULL.
 */
int hio_wait (hio_request_t *request);

/** @ingroup API
 * @brief Get recommendation on if a checkpoint should be written
 *
 * @param[in]  ctx  hio context
 * @param[out] hint Recommendation on checkpoint
 *
 * This function determines if it is optimal to checkpoint at
 * this time. This function may query the runtime or use past I/O
 * activity on the context to come up with a recommendation. If the
 * recommendation is HIO_CP_NOT_NOW the caller should not attempt
 * to write a checkpoint.
 */
void hio_should_checkpoint (hio_context_t ctx, int *hint);

/** @ingroup API
 * @brief Inform libhio that the caller must checkpoint now.
 *
 * @param[in] ctx hio context
 *
 * This function allows the caller to imform libhio that it
 * must checkpoint now.
 *
 * NTH: I think this would be a good way to push hints back down to
 * any underlying infrastructure. I don't know at this time if these
 * sort of hints will be necessary so the functionality may be
 * removed from the final API.
 */
int hio_must_checkpoint (hio_context_t ctx);

/** @ingroup configuration
 *
 * Configuration of libhio is provided through several mechanisms listed
 * here in order of priority:
 *
 *  - The API group ref configuration. Functions in this family will
 *    attempt to set the configuration or return an error.
 *
 *  - Enviornment by setting MCA_HIO_<variable>=<value>
 *
 *  - Configurations files specified by the hio_base_mca_variable_file
 *    variable. This variable is a : seperated list of the files that
 *    should be parsed at initialization.
 *
 * Keep in mind that some configuration variables apply to only a single
 * context while others apply to all contexts.
 */

/** @ingroup configuration
 * @brief Set the value of an hio configuration variable
 *
 * @param[in] ctx       hio context
 * @param[in] variable  variable to set
 * @param[in] value     new value for this variable
 *
 * This function sets the value of the given variable. The value
 * is expected to be a string representation that matches the
 * variable's type. -- NTH: this is actually a little strange and
 * probably should be reworked to match the MPI_T cvar interface.
 */
int hio_config_set_value (hio_context_t ctx, char *variable, char *value);

/** @ingroup configuration
 * @brief Get the string representation of the value of an hio
 * configuration variable.
 *
 * @param[in]  ctx       hio context
 * @param[in]  variable  variable to get the value of
 * @param[out] value     string representation of the value of {variable}
 *
 * This function gets the string value of the given variable. It is
 * the responsibility of the caller to free the string when finished.
 */
int hio_config_get_value (hio_context_t ctx, char *variable, char **value);

/** @ingroup configuration
 * @bried Parse HIO options from a lines in a file with a given prefix.
 *
 * @param[in] ctx     hio context
 * @param[in] name    file to parse
 * @param[in] prefix  prefix of lines to parse (may be NULL)
 *
 * @returns hio_error_t
 *
 * This function parses a file looking fir HIO options. If a prefix is given in
 * {prefix} this function will only look at lines with the give prefix. If {prefix}
 * is NULL then "hio." will be used as the prefix.
 *
 * Example:
 *
 * File input.deck contains:
 *   hio.file_stage_mode = lazy
 *
 * Then:
 *   hio_config_parse_file (ctx, "input.deck", NULL);
 *
 * Is equivalent to calling:
 *   hio_config_set_value (ctx, "file_stage_mode", "lazy");
 */
int hio_config_parse_file (hio_context_t ctx, const char *name, const char *prefix);

/** @ingroup configuration
 * @brief Get the number of configuration variables
 *
 * @param[in]  ctx     hio context
 * @param[out] count   the number of configuration variables
 *
 * @returns hio_error_t
 */
int hio_config_get_count (hio_context_t ctx, int *count);

/** @ingroup configuration
 * @brief List all HIO variables
 *
 * @param[in]    ctx        hio context
 * @param[inout] variables  pointer to store variable names
 * @param[in]    count      size of {variables} array
 *
 * @returns hio_error_t
 *
 * This function gets the names of the variables and stores them in the variable
 * array. Up to {count} variables will be stored. It is up to the user to free
 * the strings stored in {variables}.
 */
int hio_config_list_variables (hio_context_t ctx, char **variables, int count);

#endif /* !defined(HIO_H) */
