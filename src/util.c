#define STB_SPRINTF_IMPLEMENTATION
#include "purpl/util.h"

#ifdef __cplusplus
extern "C" {
#endif

char *purpl_fmt_text_va(size_t *len_ret, const char *fmt,
				     va_list args)
{
	size_t len;
	char *buf;
	va_list ap;

	PURPL_RESET_ERRNO;

	/* Check our parameters */
	if (!len_ret || !fmt || !args) {
		errno = EINVAL;
		return fmt;
	}

	/* Copy the arglist */
	va_copy(ap, args);

	/* Determine how large the buffer has to be */
	len = stbsp_vsnprintf(NULL, 0, fmt, args) + 1;
	if (!len) {
		errno = E2BIG;
		len = PURPL_LARGE_BUF;
	}

	/* Now we know how big the buffer will be */
	buf = PURPL_CALLOC(len, char);
	if (!buf) {
		errno = ENOMEM;
		len = -1;
		return fmt; /*
			     * This function is used a lot and if it can't
			     *  "fail" that's good
			     */
	}

	/* Now put the string in the buffer and return */
	stbsp_vsnprintf(buf, len, fmt, ap);
	if (!buf) {
		errno = E2BIG;
		len = -1;
		return fmt;
	}
	*len_ret = len;

	PURPL_RESET_ERRNO;
	return buf;
}

char *purpl_fmt_text(size_t *len_ret, const char *fmt, ...)
{
	va_list args;
	char *fmt_ptr;

	PURPL_RESET_ERRNO;

	/* Check everything */
	if (!len_ret || !fmt) {
		errno = EINVAL;
		return fmt;
	}

	va_start(args, fmt);
	fmt_ptr = purpl_fmt_text_va(len_ret, fmt, args);
	va_end(args);

	PURPL_RESET_ERRNO;

	return fmt_ptr;
}

struct purpl_mapping *purpl_map_file(u8 protection, FILE *fp)
{
	struct purpl_mapping *mapping;
	u8 prot;
	int fd;
	int fd2;
	size_t fpos;

	PURPL_RESET_ERRNO;

	/* Check arguments */
	if (!fp) {
		errno = EINVAL;
		return NULL;
	}

	/* Allocate the mapping information */
	mapping = PURPL_CALLOC(1, struct purpl_mapping);

	/* Fix up protection (limit it to 2) */
	prot = protection & 0xF;

	/* Figure out how thicc the file is */
	fpos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	mapping->len = ftell(fp);
	fseek(fp, 0, fpos);

	/*
	 * Why in K&R's great creation does Windows
	 *  even _have_ file descriptors _and_ handles?!
	 *  In any case, it does make this easier.
	 */
	fd = fileno(fp);
	if (fd < 0) {
		errno = EBADF;
		return NULL;
	}

	/* Duplicate fd just in case */
	fd2 = dup(fd);

/* You have to call so many weird functions for this on Windows */
#ifdef _WIN32
	/*
	 * You have to go through THREE different types if you start with a
	 *  FILE * in order to map a file on Windows, which is *great* >:(
	 */
	HANDLE file;

	/* Now get a handle to the file */
	file = _get_osfhandle(fd);

	/* Determine protection */
	switch (prot) {
	case 0:
		prot = FILE_MAP_READ;
		break;
	case 1:
		prot = FILE_MAP_WRITE;
		break;
	case 2:
		prot = FILE_MAP_WRITE | FILE_MAP_EXECUTE;
		break;
	}

	/* Create a mapping object, whatever the fuck that's meant to be */
	mapping->handle = CreateFileMappingA(file, NULL, PAGE_EXECUTE_READWRITE,
					     0, PURPL_HIGH(mapping->len, DWORD),
					     PURPL_LOW(mapping->len, DWORD), NULL);
	if (!mapping->handle) {
		errno = ENOMEM;
		return NULL;
	}

	/* Create a "view" of the mapping */
	mapping->data = MapViewOfFile(mapping->handle, prot, 0, 0, mapping->len);
	if (!mapping->data) {
		/* 
		 * Microsoft brought this upon us by having
		 *  their own weird-ass system for error codes
		 */
		if (GetLastError() == ERROR_ACCESS_DENIED)
			errno = EPERM;
		else
			errno = ENOMEM;
		return NULL;
	}
#else /*
       * Everything else is POSIX and therefore simple, sane,
       * logical, and consistent
       */
	/* Determine protection */
	switch (prot) {
	case 0:
		prot = PROT_READ;
		break;
	case 1:
		prot = PROT_READ | PROT_WRITE;
		break;
	case 2:
		prot = PROT_READ | PROT_WRITE | PROT_EXEC;
		break;
	}

	/* Map the file */
	PURPL_RESET_ERRNO;
	PURPL_RETRY_INTR(mapping->data =
				 mmap(NULL, mapping->len, prot,
				      (prot - 1) ? MAP_SHARED : MAP_PRIVATE,
				      fd2, 0));

	/* If a permission error arises, downgrade requested access */
	if (!mapping->data && errno == EPERM) {
		prot--;
		PURPL_RESET_ERRNO;
		PURPL_RETRY_INTR(mapping->data = mmap(NULL, mapping->len,
						      prot - 1, MAP_SHARED, fd2,
						      0));
	}

	if (!mapping->data)
		/* errno is already initialized to something valid */
		return NULL;
	
	/* Do some final error checking */
	if (errno)
		return NULL;
#endif /* _WIN32 */

	/* Close fd2 */
	close(fd2);

	PURPL_RESET_ERRNO;

	/* Return the map */
	return mapping;
}

void purpl_unmap_file(struct purpl_mapping *mapping)
{
	PURPL_RESET_ERRNO;

	/* Check argument */
	if (!mapping) {
		errno = EINVAL;
		return;
	}

	/* Begin all that nasty platform-specific shit */
#ifdef _WIN32
	/* Unmap the file view and close the handle to the mapping object */
	UnmapViewOfFile(mapping->data);
	CloseHandle(mapping->handle);
#else
	/* Unmap the file */
	PURPL_RETRY_INTR(munmap(mapping->data, mapping->len));
#endif

	/* Free info */
	free(mapping);

	PURPL_RESET_ERRNO;
}

char *purpl_read_file_fp(size_t *len_ret,
				      struct purpl_mapping **mapping, bool map,
				      FILE *fp)
{
	size_t len;
	char *file;
	bool will_map = map;

	PURPL_RESET_ERRNO;

	/* Validate arguments */
	if (!len_ret || !fp) {
		errno = EINVAL;
		return NULL;
	}

	/*
	 * If something goes wrong, set map to false so that
	 * the file (hopefully) still gets read
	 */
	if (will_map) {
		struct purpl_mapping *map;

		/* Check if mapping is NULL */
		if (!mapping) {
			errno = EINVAL;
			will_map = false;
		}

		/* Map the file */
		map = purpl_map_file(1, fp);
		if (!mapping)
			will_map = false;

		/* Return mapping */
		memcpy(mapping, &map, sizeof(void *));

		/* Set file and len */
		file = map->data;
		len = map->len;
	}

	/* Either mapping wasn't requested, or it failed */
	if (!will_map) {
		size_t fpos;

		/* Determine file length */
		fpos = ftell(fp);
		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		fseek(fp, 0, fpos);

		/* Allocate a buffer */
		file = PURPL_CALLOC(len + 1, char);
		if (!file)
			return NULL;

		/* Read into the buffer */
		len = fread(file, sizeof(char), len, fp);
		if (!file)
			return file;

		/* Ensure it's terminated */
		file[len] = 0;

		/* Put fp bainfopping is NULL */
		mapping = NULL;
	}

	PURPL_RESET_ERRNO;

	/* Return */
	*len_ret = len;
	return file;
}

char *purpl_read_file(size_t *len_ret, struct purpl_mapping **info,
				   bool map, const char *path, ...)
{
	va_list args;
	char *path_fmt;
	size_t path_len;
	FILE *fp;

	/* Check args */
	if (!len_ret || !path || (map && !info)) {
		errno = EINVAL;
		return NULL;
	}

	/* Format the path to the file */
	va_start(args, path);
	path_fmt = purpl_fmt_text_va(&path_len, path, args);
	va_end(args);

	/* Open the file */
	fp = fopen(path_fmt, PURPL_WRITE);
	if (!fp)
		return NULL;

	PURPL_RESET_ERRNO;

	/* Return read_file_fp */
	return purpl_read_file_fp(len_ret, info, map, fp);
}

#ifdef __cplusplus
}
#endif
