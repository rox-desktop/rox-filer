/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _SUPPORT_H
#define _SUPPORT_H

#define PRETTY_SIZE_LIMIT 4096
#define TIME_FORMAT "%T %d %b %Y"

char *pathdup(char *path);
GString *make_path(char *dir, char *leaf);
char *our_host_name();
pid_t spawn(char **argv);
pid_t spawn_full(char **argv, char *dir);
void debug_free_string(void *data);
char *user_name(uid_t uid);
char *group_name(gid_t gid);
char *format_size(unsigned long size);
char *format_size_aligned(unsigned long size);
gchar *format_double_size_brief(double size);
int fork_exec_wait(char **argv);
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
void add_default_styles(void);

#endif /* _SUPPORT_H */
