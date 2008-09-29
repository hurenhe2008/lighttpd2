
#include "base.h"
#include "http_headers.h"

static void _http_header_free(gpointer p) {
	http_header *h = (http_header*) p;
	g_string_free(h->data, TRUE);
	g_slice_free(http_header, h);
}

static http_header* _http_header_new(const gchar *key, size_t keylen, const gchar *val, size_t valuelen) {
	http_header *h = g_slice_new0(http_header);
	h->data = g_string_sized_new(keylen + valuelen + 2);
	h->keylen = keylen;
	g_string_append_len(h->data, key, keylen);
	g_string_append_len(h->data, CONST_STR_LEN(": "));
	g_string_append_len(h->data, val, valuelen);
	return h;
}

static void _header_queue_free(gpointer data, gpointer userdata) {
	UNUSED(userdata);
	_http_header_free((http_header*) data);
}

http_headers* http_headers_new() {
	http_headers* headers = g_slice_new0(http_headers);
	headers->refcount = 1;
	g_queue_init(&headers->entries);
	return headers;
}

static void http_headers_reset(http_headers* headers) {
	g_queue_foreach(&headers->entries, _header_queue_free, NULL);
	g_queue_clear(&headers->entries);
}

static void http_headers_free(http_headers* headers) {
	if (!headers) return;
	g_queue_foreach(&headers->entries, _header_queue_free, NULL);
	g_queue_clear(&headers->entries);
	g_slice_free(http_headers, headers);
}

void http_headers_acquire(http_headers* headers) {
	assert(g_atomic_int_get(&headers->refcount) > 0);
	g_atomic_int_inc(&headers->refcount);
}

void http_headers_release(http_headers* headers) {
	if (!headers) return;
	assert(g_atomic_int_get(&headers->refcount) > 0);
	if (g_atomic_int_dec_and_test(&headers->refcount)) {
		http_headers_free(headers);
	}
}

http_headers* http_headers_try_reset(http_headers* headers) {
	assert(g_atomic_int_get(&headers->refcount) > 0);
	if (g_atomic_int_dec_and_test(&headers->refcount)) {
		http_headers_reset(headers);
		headers->refcount = 1;
		return headers;
	} else {
		return http_headers_new();
	}
}

/** just insert normal header, allow duplicates */
void http_header_insert(http_headers *headers, const gchar *key, size_t keylen, const gchar *val, size_t valuelen) {
	http_header *h = _http_header_new(key, keylen, val, valuelen);
	g_queue_push_tail(&headers->entries, h);
}

GList* http_header_find_first(http_headers *headers, const gchar *key, size_t keylen) {
	http_header *h;
	GList *l;

	for (l = g_queue_peek_head_link(&headers->entries); l; l = g_list_next(l)) {
		h = (http_header*) l->data;
		if (h->keylen == keylen && 0 == g_ascii_strncasecmp(key, h->data->str, keylen)) return l;
	}
	return NULL;
}

GList* http_header_find_next(GList *l, const gchar *key, size_t keylen) {
	http_header *h;

	for (l = g_list_next(l); l; l = g_list_next(l)) {
		h = (http_header*) l->data;
		if (h->keylen == keylen && 0 == g_ascii_strncasecmp(key, h->data->str, keylen)) return l;
	}
	return NULL;
}

GList* http_header_find_last(http_headers *headers, const gchar *key, size_t keylen) {
	http_header *h;
	GList *l;

	for (l = g_queue_peek_tail_link(&headers->entries); l; l = g_list_previous(l)) {
		h = (http_header*) l->data;
		if (h->keylen == keylen && 0 == g_ascii_strncasecmp(key, h->data->str, keylen)) return l;
	}
	return NULL;
}

/** If header does not exist, just insert normal header. If it exists, append (", %s", value) to the last inserted one */
void http_header_append(http_headers *headers, const gchar *key, size_t keylen, const gchar *val, size_t valuelen) {
	GList *l;
	http_header *h;

	l = http_header_find_last(headers, key, keylen);
	if (NULL == l) {
		http_header_insert(headers, key, keylen, val, valuelen);
	} else {
		h = (http_header*) l;
		g_string_append_len(h->data, CONST_STR_LEN(", "));
		g_string_append_len(h->data, val, valuelen);
	}
}

/** If header does not exist, just insert normal header. If it exists, overwrite the last occurrence */
void http_header_overwrite(http_headers *headers, const gchar *key, size_t keylen, const gchar *val, size_t valuelen) {
	GList *l;
	http_header *h;

	l = http_header_find_last(headers, key, keylen);
	if (NULL == l) {
		http_header_insert(headers, key, keylen, val, valuelen);
	} else {
		h = (http_header*) l;
		g_string_truncate(h->data, 0);
		g_string_append_len(h->data, key, keylen);
		g_string_append_len(h->data, CONST_STR_LEN(": "));
		g_string_append_len(h->data, val, valuelen);
	}
}

void http_header_remove_link(http_headers *headers, GList *l) {
	_http_header_free(l->data);
	g_queue_delete_link(&headers->entries, l);
}

gboolean http_header_remove(http_headers *headers, const gchar *key, size_t keylen) {
	GList *l, *lp = NULL;;
	gboolean res = FALSE;

	for (l = http_header_find_first(headers, key, keylen); l; l = http_header_find_next(l, key, keylen)) {
		if (lp) {
			http_header_remove_link(headers, lp);
			res = TRUE;
			lp = NULL;
		}
		lp = l;
	}
	if (lp) {
		http_header_remove_link(headers, lp);
		res = TRUE;
		lp = NULL;
	}
	return res;
}

http_header* http_header_lookup(http_headers *headers, const gchar *key, size_t keylen) {
	GList *l;

	l = http_header_find_last(headers, key, keylen);
	return NULL == l ? NULL : (http_header*) l->data;
}

gboolean http_header_is(http_headers *headers, const gchar *key, size_t keylen, const gchar *val, size_t valuelen) {
	GList *l;
	UNUSED(valuelen);

	for (l = http_header_find_first(headers, key, keylen); l; l = http_header_find_next(l, key, keylen)) {
		http_header *h = (http_header*) l->data;
		if (h->data->len - (h->keylen + 2) != valuelen) continue;
		if (0 == g_ascii_strcasecmp( &h->data->str[h->keylen+2], val )) return TRUE;
	}
	return FALSE;
}

void http_header_get_fast(GString *dest, http_headers *headers, const gchar *key, size_t keylen) {
	GList *l;
	g_string_truncate(dest, 0);

	for (l = http_header_find_first(headers, key, keylen); l; l = http_header_find_next(l, key, keylen)) {
		http_header *h = (http_header*) l->data;
		if (dest->len) g_string_append_len(dest, CONST_STR_LEN(", "));
		g_string_append_len(dest, &h->data->str[h->keylen+2], h->data->len - (h->keylen + 2));
	}
}
