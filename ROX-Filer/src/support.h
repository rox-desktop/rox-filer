/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _SUPPORT_H
#define _SUPPORT_H

struct _XMLwrapper {
	int	ref;
	xmlDocPtr doc;
};

#define PRETTY_SIZE_LIMIT 10000
#define TIME_FORMAT "%T %d %b %Y"

XMLwrapper *xml_cache_load(gchar *pathname);
void xml_cache_unref(XMLwrapper *wrapper);
int save_xml_file(xmlDocPtr doc, gchar *filename);
xmlNode *get_subnode(xmlNode *node, const char *namespaceURI, const char *name);
xmlDocPtr soap_new(xmlNodePtr *ret_body);
char *pathdup(char *path);
GString *make_path(char *dir, char *leaf);
char *our_host_name();
char *our_host_name_for_dnd();
pid_t spawn_full(char **argv, char *dir, int *to_stderr);
void debug_free_string(void *data);
char *user_name(uid_t uid);
char *group_name(gid_t gid);
char *format_size(off_t size);
char *format_size_aligned(off_t size);
gchar *format_double_size(double size);
char *fork_exec_wait(char **argv);
char *pretty_permissions(mode_t m);
gint applicable(uid_t uid, gid_t gid);
char *get_local_path(char *uri);
void close_on_exec(int fd, gboolean close);
void set_blocking(int fd, gboolean blocking);
char *pretty_time(time_t *time);
guchar *copy_file(guchar *from, guchar *to);
guchar *shell_escape(guchar *word);
gboolean is_sub_dir(char *sub, char *parent);
gboolean in_list(guchar *item, guchar *list);
GPtrArray *split_path(guchar *path);
guchar *get_relative_path(guchar *from, guchar *to);
int text_to_boolean(const char *text, int defvalue);
void set_to_null(gpointer *data);
char *readlink_dup(char *path);

#ifdef GTK2
char *md5_hash(char *message);
#endif

#endif /* _SUPPORT_H */
