/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _SUPPORT_H
#define _SUPPORT_H

#define PRETTY_SIZE_LIMIT 10000
#define TIME_FORMAT "%T %d %b %Y"

XMLwrapper *xml_cache_load(const gchar *pathname);
int save_xml_file(xmlDocPtr doc, const gchar *filename);
xmlNode *get_subnode(xmlNode *node, const char *namespaceURI, const char *name);
xmlDocPtr soap_new(xmlNodePtr *ret_body);
char *pathdup(const char *path);
GString *make_path(const char *dir, const char *leaf);
const char *our_host_name();
const char *our_host_name_for_dnd();
pid_t spawn_full(const char **argv, const char *dir, int *to_stderr);
void debug_free_string(void *data);
const char *user_name(uid_t uid);
const char *group_name(gid_t gid);
char *format_size(off_t size);
char *format_size_aligned(off_t size);
gchar *format_double_size(double size);
char *fork_exec_wait(const char **argv);
char *pretty_permissions(mode_t m);
gint applicable(uid_t uid, gid_t gid);
const char *get_local_path(const char *uri);
void close_on_exec(int fd, gboolean close);
void set_blocking(int fd, gboolean blocking);
char *pretty_time(time_t *time);
guchar *copy_file(const guchar *from, const guchar *to);
guchar *shell_escape(const guchar *word);
gboolean is_sub_dir(const char *sub, const char *parent);
gboolean in_list(const guchar *item, const guchar *list);
GPtrArray *split_path(const guchar *path);
guchar *get_relative_path(const guchar *from, const guchar *to);
int text_to_boolean(const char *text, int defvalue);
char *readlink_dup(const char *path);
gchar *to_utf8(const gchar *src);
gchar *from_utf8(const gchar *src);
char *md5_hash(const char *message);

#endif /* _SUPPORT_H */
