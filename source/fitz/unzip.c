#include "mupdf/fitz.h"

#include <zlib.h>

#if !defined (INT32_MAX)
#define INT32_MAX 2147483647L
#endif

#define ZIP_LOCAL_FILE_SIG 0x04034b50
#define ZIP_DATA_DESC_SIG 0x08074b50
#define ZIP_CENTRAL_DIRECTORY_SIG 0x02014b50
#define ZIP_END_OF_CENTRAL_DIRECTORY_SIG 0x06054b50

#define ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIG 0x07064b50
#define ZIP64_END_OF_CENTRAL_DIRECTORY_SIG 0x06064b50
#define ZIP64_EXTRA_FIELD_SIG 0x0001

#define ZIP_ENCRYPTED_FLAG 0x1

struct zip_entry
{
	char *name;
	int offset, csize, usize;
};

struct fz_archive_s
{
	char *directory;
	fz_stream *file;
	int count;
	struct zip_entry *table;
};

static inline int zip_toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 'A';
	return c;
}

static int zip_strcasecmp(const char *a, const char *b)
{
	while (zip_toupper(*a) == zip_toupper(*b))
	{
		if (*a++ == 0)
			return 0;
		b++;
	}
	return zip_toupper(*a) - zip_toupper(*b);
}

static int case_compare_entries(const void *a_, const void *b_)
{
	const struct zip_entry *a = a_;
	const struct zip_entry *b = b_;
	return zip_strcasecmp(a->name, b->name);
}

static struct zip_entry *lookup_zip_entry(fz_context *ctx, fz_archive *zip, const char *name)
{
	int l = 0;
	int r = zip->count - 1;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		int c = zip_strcasecmp(name, zip->table[m].name);
		if (c < 0)
			r = m - 1;
		else if (c > 0)
			l = m + 1;
		else
			return &zip->table[m];
	}
	return NULL;
}

static void read_zip_dir_imp(fz_context *ctx, fz_archive *zip, int start_offset)
{
	fz_stream *file = zip->file;
	int sig;
	int offset, count;
	int namesize, metasize, commentsize;
	int i;

	fz_seek(ctx, file, start_offset, 0);

	sig = fz_read_int32_le(ctx, file);
	if (sig != ZIP_END_OF_CENTRAL_DIRECTORY_SIG)
		fz_throw(ctx, FZ_ERROR_GENERIC, "wrong zip end of central directory signature (0x%x)", sig);

	(void) fz_read_int16_le(ctx, file); /* this disk */
	(void) fz_read_int16_le(ctx, file); /* start disk */
	(void) fz_read_int16_le(ctx, file); /* entries in this disk */
	count = fz_read_int16_le(ctx, file); /* entries in central directory disk */
	(void) fz_read_int32_le(ctx, file); /* size of central directory */
	offset = fz_read_int32_le(ctx, file); /* offset to central directory */

	/* ZIP64 */
	if (count == 0xFFFF || offset == 0xFFFFFFFF)
	{
		int64_t offset64, count64;

		fz_seek(ctx, file, start_offset - 20, 0);

		sig = fz_read_int32_le(ctx, file);
		if (sig != ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIG)
			fz_throw(ctx, FZ_ERROR_GENERIC, "wrong zip64 end of central directory locator signature (0x%x)", sig);

		(void) fz_read_int32_le(ctx, file); /* start disk */
		offset64 = fz_read_int64_le(ctx, file); /* offset to end of central directory record */
		if (offset64 > INT32_MAX)
			fz_throw(ctx, FZ_ERROR_GENERIC, "zip64 files larger than 2 GB aren't supported");

		fz_seek(ctx, file, offset64, 0);

		sig = fz_read_int32_le(ctx, file);
		if (sig != ZIP64_END_OF_CENTRAL_DIRECTORY_SIG)
			fz_throw(ctx, FZ_ERROR_GENERIC, "wrong zip64 end of central directory signature (0x%x)", sig);

		(void) fz_read_int64_le(ctx, file); /* size of record */
		(void) fz_read_int16_le(ctx, file); /* version made by */
		(void) fz_read_int16_le(ctx, file); /* version to extract */
		(void) fz_read_int32_le(ctx, file); /* disk number */
		(void) fz_read_int32_le(ctx, file); /* disk number start */
		count64 = fz_read_int64_le(ctx, file); /* entries in central directory disk */
		(void) fz_read_int64_le(ctx, file); /* entries in central directory */
		(void) fz_read_int64_le(ctx, file); /* size of central directory */
		offset64 = fz_read_int64_le(ctx, file); /* offset to central directory */

		if (count == 0xFFFF)
		{
			if (count64 > INT32_MAX)
				fz_throw(ctx, FZ_ERROR_GENERIC, "zip64 files larger than 2 GB aren't supported");
			count = count64;
		}
		if (offset == 0xFFFFFFFF)
		{
			if (offset64 > INT32_MAX)
				fz_throw(ctx, FZ_ERROR_GENERIC, "zip64 files larger than 2 GB aren't supported");
			offset = offset64;
		}
	}

	zip->count = count;
	zip->table = fz_malloc_array(ctx, count, sizeof *zip->table);
	memset(zip->table, 0, count * sizeof *zip->table);

	fz_seek(ctx, file, offset, 0);

	for (i = 0; i < count; i++)
	{
		sig = fz_read_int32_le(ctx, file);
		if (sig != ZIP_CENTRAL_DIRECTORY_SIG)
			fz_throw(ctx, FZ_ERROR_GENERIC, "wrong zip central directory signature (0x%x)", sig);

		(void) fz_read_int16_le(ctx, file); /* version made by */
		(void) fz_read_int16_le(ctx, file); /* version to extract */
		(void) fz_read_int16_le(ctx, file); /* general */
		(void) fz_read_int16_le(ctx, file); /* method */
		(void) fz_read_int16_le(ctx, file); /* last mod file time */
		(void) fz_read_int16_le(ctx, file); /* last mod file date */
		(void) fz_read_int32_le(ctx, file); /* crc-32 */
		zip->table[i].csize = fz_read_int32_le(ctx, file);
		zip->table[i].usize = fz_read_int32_le(ctx, file);
		namesize = fz_read_int16_le(ctx, file);
		metasize = fz_read_int16_le(ctx, file);
		commentsize = fz_read_int16_le(ctx, file);
		(void) fz_read_int16_le(ctx, file); /* disk number start */
		(void) fz_read_int16_le(ctx, file); /* int file atts */
		(void) fz_read_int32_le(ctx, file); /* ext file atts */
		zip->table[i].offset = fz_read_int32_le(ctx, file);

		zip->table[i].name = fz_malloc(ctx, namesize + 1);
		fz_read(ctx, file, (unsigned char*)zip->table[i].name, namesize);
		zip->table[i].name[namesize] = 0;

		while (metasize > 0)
		{
			int type = fz_read_int16_le(ctx, file);
			int size = fz_read_int16_le(ctx, file);
			if (type == ZIP64_EXTRA_FIELD_SIG)
			{
				int sizeleft = size;
				if (zip->table[i].usize == 0xFFFFFFFF && sizeleft >= 8)
				{
					zip->table[i].usize = fz_read_int64_le(ctx, file);
					sizeleft -= 8;
				}
				if (zip->table[i].csize == 0xFFFFFFFF && sizeleft >= 8)
				{
					zip->table[i].csize = fz_read_int64_le(ctx, file);
					sizeleft -= 8;
				}
				if (zip->table[i].offset == 0xFFFFFFFF && sizeleft >= 8)
				{
					zip->table[i].offset = fz_read_int64_le(ctx, file);
					sizeleft -= 8;
				}
				fz_seek(ctx, file, sizeleft - size, 1);
			}
			fz_seek(ctx, file, size, 1);
			metasize -= 4 + size;
		}
		if (zip->table[i].usize < 0 || zip->table[i].csize < 0 || zip->table[i].offset < 0)
			fz_throw(ctx, FZ_ERROR_GENERIC, "zip64 files larger than 2 GB are not supported");

		fz_seek(ctx, file, commentsize, 1);
	}

	qsort(zip->table, count, sizeof *zip->table, case_compare_entries);
}

static void read_zip_dir(fz_context *ctx, fz_archive *zip)
{
	fz_stream *file = zip->file;
	unsigned char buf[512];
	size_t size, back, maxback;
	size_t i, n;

	fz_seek(ctx, file, 0, SEEK_END);
	size = fz_tell(ctx, file);

	maxback = fz_minz(size, 0xFFFF + sizeof buf);
	back = fz_minz(maxback, sizeof buf);

	while (back < maxback)
	{
		fz_seek(ctx, file, size - back, 0);
		n = fz_read(ctx, file, buf, sizeof buf);
		if (n < 4)
			break;
		for (i = n - 4; i > 0; i--)
		{
			if (!memcmp(buf + i, "PK\5\6", 4))
			{
				read_zip_dir_imp(ctx, zip, (int)(size - back + i));
				return;
			}
		}
		back += sizeof buf - 4;
	}

	fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find end of central directory");
}

static int read_zip_entry_header(fz_context *ctx, fz_archive *zip, struct zip_entry *ent)
{
	fz_stream *file = zip->file;
	int sig, general, method, namelength, extralength;

	fz_seek(ctx, file, ent->offset, 0);

	sig = fz_read_int32_le(ctx, file);
	if (sig != ZIP_LOCAL_FILE_SIG)
		fz_throw(ctx, FZ_ERROR_GENERIC, "wrong zip local file signature (0x%x)", sig);

	(void) fz_read_int16_le(ctx, file); /* version */
	general = fz_read_int16_le(ctx, file); /* general */
	if (general & ZIP_ENCRYPTED_FLAG)
		fz_throw(ctx, FZ_ERROR_GENERIC, "zip content is encrypted");

	method = fz_read_int16_le(ctx, file);
	(void) fz_read_int16_le(ctx, file); /* file time */
	(void) fz_read_int16_le(ctx, file); /* file date */
	(void) fz_read_int32_le(ctx, file); /* crc-32 */
	(void) fz_read_int32_le(ctx, file); /* csize */
	(void) fz_read_int32_le(ctx, file); /* usize */
	namelength = fz_read_int16_le(ctx, file);
	extralength = fz_read_int16_le(ctx, file);

	fz_seek(ctx, file, namelength + extralength, 1);

	return method;
}

static fz_stream *open_zip_entry(fz_context *ctx, fz_archive *zip, struct zip_entry *ent)
{
	fz_stream *file = zip->file;
	int method = read_zip_entry_header(ctx, zip, ent);
	if (method == 0)
		return fz_open_null(ctx, file, ent->usize, fz_tell(ctx, file));
	if (method == 8)
		return fz_open_flated(ctx, file, -15);
	fz_throw(ctx, FZ_ERROR_GENERIC, "unknown zip method: %d", method);
}

static fz_buffer *read_zip_entry(fz_context *ctx, fz_archive *zip, struct zip_entry *ent)
{
	fz_stream *file = zip->file;
	fz_buffer *ubuf;
	unsigned char *cbuf;
	int method;
	z_stream z;
	int code;
	int len;

	method = read_zip_entry_header(ctx, zip, ent);

	ubuf = fz_new_buffer(ctx, ent->usize + 1); /* +1 because many callers will add a terminating zero */
	ubuf->len = ent->usize;

	if (method == 0)
	{
		fz_try(ctx)
		{
			len = fz_read(ctx, file, ubuf->data, ubuf->len);
			if (len < ubuf->len)
				fz_warn(ctx, "premature end of data in stored archive entry");
			ubuf->len = len;
		}
		fz_catch(ctx)
		{
			fz_drop_buffer(ctx, ubuf);
			fz_rethrow(ctx);
		}
		return ubuf;
	}

	if (method == 8)
	{
		cbuf = fz_malloc(ctx, ent->csize);
		fz_try(ctx)
		{
			fz_read(ctx, file, cbuf, ent->csize);

			z.zalloc = (alloc_func) fz_malloc_array;
			z.zfree = (free_func) fz_free;
			z.opaque = ctx;
			z.next_in = cbuf;
			z.avail_in = ent->csize;
			z.next_out = ubuf->data;
			z.avail_out = ent->usize;

			code = inflateInit2(&z, -15);
			if (code != Z_OK)
			{
				fz_throw(ctx, FZ_ERROR_GENERIC, "zlib inflateInit2 error: %s", z.msg);
			}
			code = inflate(&z, Z_FINISH);
			if (code != Z_STREAM_END)
			{
				inflateEnd(&z);
				fz_throw(ctx, FZ_ERROR_GENERIC, "zlib inflate error: %s", z.msg);
			}
			code = inflateEnd(&z);
			if (code != Z_OK)
			{
				fz_throw(ctx, FZ_ERROR_GENERIC, "zlib inflateEnd error: %s", z.msg);
			}

			len = ent->usize - z.avail_out;
			if (len < ubuf->len)
				fz_warn(ctx, "premature end of data in compressed archive entry");
			ubuf->len = len;
		}
		fz_always(ctx)
		{
			fz_free(ctx, cbuf);
		}
		fz_catch(ctx)
		{
			fz_drop_buffer(ctx, ubuf);
			fz_rethrow(ctx);
		}
		return ubuf;
	}

	fz_drop_buffer(ctx, ubuf);
	fz_throw(ctx, FZ_ERROR_GENERIC, "unknown zip method: %d", method);
}

int
fz_has_archive_entry(fz_context *ctx, fz_archive *zip, const char *name)
{
	if (zip->directory)
	{
		char path[2048];
		fz_strlcpy(path, zip->directory, sizeof path);
		fz_strlcat(path, "/", sizeof path);
		fz_strlcat(path, name, sizeof path);
		return fz_file_exists(ctx, path);
	}
	else
	{
		return lookup_zip_entry(ctx, zip, name) != NULL;
	}
}

fz_stream *
fz_open_archive_entry(fz_context *ctx, fz_archive *zip, const char *name)
{
	if (zip->directory)
	{
		char path[2048];
		fz_strlcpy(path, zip->directory, sizeof path);
		fz_strlcat(path, "/", sizeof path);
		fz_strlcat(path, name, sizeof path);
		return fz_open_file(ctx, path);
	}
	else
	{
		struct zip_entry *ent = lookup_zip_entry(ctx, zip, name);
		if (!ent)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find zip entry: '%s'", name);
		return open_zip_entry(ctx, zip, ent);
	}
}

fz_buffer *
fz_read_archive_entry(fz_context *ctx, fz_archive *zip, const char *name)
{
	if (zip->directory)
	{
		char path[2048];
		fz_strlcpy(path, zip->directory, sizeof path);
		fz_strlcat(path, "/", sizeof path);
		fz_strlcat(path, name, sizeof path);
		return fz_read_file(ctx, path);
	}
	else
	{
		struct zip_entry *ent = lookup_zip_entry(ctx, zip, name);
		if (!ent)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find zip entry: '%s'", name);
		return read_zip_entry(ctx, zip, ent);
	}
}

int
fz_count_archive_entries(fz_context *ctx, fz_archive *zip)
{
	return zip->count;
}

const char *
fz_list_archive_entry(fz_context *ctx, fz_archive *zip, int idx)
{
	if (idx < 0 || idx >= zip->count)
		return NULL;
	return zip->table[idx].name;
}

void
fz_drop_archive(fz_context *ctx, fz_archive *zip)
{
	int i;
	if (zip)
	{
		fz_free(ctx, zip->directory);
		fz_drop_stream(ctx, zip->file);
		for (i = 0; i < zip->count; ++i)
			fz_free(ctx, zip->table[i].name);
		fz_free(ctx, zip->table);
		fz_free(ctx, zip);
	}
}

fz_archive *
fz_open_directory(fz_context *ctx, const char *dirname)
{
	fz_archive *zip = fz_malloc_struct(ctx, fz_archive);
	zip->directory = fz_strdup(ctx, dirname);
	return zip;
}

fz_archive *
fz_open_archive_with_stream(fz_context *ctx, fz_stream *file)
{
	fz_archive *zip;

	zip = fz_malloc_struct(ctx, fz_archive);
	zip->file = fz_keep_stream(ctx, file);
	zip->count = 0;
	zip->table = NULL;

	fz_try(ctx)
	{
		read_zip_dir(ctx, zip);
	}
	fz_catch(ctx)
	{
		fz_drop_archive(ctx, zip);
		fz_rethrow(ctx);
	}

	return zip;
}

fz_archive *
fz_open_archive(fz_context *ctx, const char *filename)
{
	fz_stream *file;
	fz_archive *zip;

	file = fz_open_file(ctx, filename);

	fz_try(ctx)
		zip = fz_open_archive_with_stream(ctx, file);
	fz_always(ctx)
		fz_drop_stream(ctx, file);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return zip;
}
