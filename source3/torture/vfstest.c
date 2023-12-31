/*
   Unix SMB/CIFS implementation.
   VFS module tester

   Copyright (C) Simo Sorce 2002
   Copyright (C) Eric Lorimer 2002
   Copyright (C) Jelmer Vernooij 2002,2003

   Most of this code was ripped off of rpcclient.
   Copyright (C) Tim Potter 2000-2001

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "locking/share_mode_lock.h"
#include "smbd/smbd.h"
#include "smbd/globals.h"
#include "lib/cmdline/cmdline.h"
#include "vfstest.h"
#include "../libcli/smbreadline/smbreadline.h"
#include "auth.h"
#include "serverid.h"
#include "messages.h"
#include "libcli/security/security.h"
#include "lib/smbd_shim.h"
#include "system/filesys.h"
#include "lib/global_contexts.h"
#include "lib/param/param.h"

/* List to hold groups of commands */
static struct cmd_list {
	struct cmd_list *prev, *next;
	struct cmd_set *cmd_set;
} *cmd_list;

/* shall we do talloc_report after each command? */
static int memreports = 0;

/****************************************************************************
handle completion of commands for readline
****************************************************************************/
static char **completion_fn(const char *text, int start, int end)
{
#define MAX_COMPLETIONS 100
	char **matches;
	int i, count=0;
	struct cmd_list *commands = cmd_list;

	if (start)
		return NULL;

	/* make sure we have a list of valid commands */
	if (!commands)
		return NULL;

	matches = SMB_MALLOC_ARRAY(char *, MAX_COMPLETIONS);
	if (!matches) return NULL;

	matches[count++] = SMB_STRDUP(text);
	if (!matches[0]) return NULL;

	while (commands && count < MAX_COMPLETIONS-1)
	{
		if (!commands->cmd_set)
			break;

		for (i=0; commands->cmd_set[i].name; i++)
		{
			if ((strncmp(text, commands->cmd_set[i].name, strlen(text)) == 0) &&
				commands->cmd_set[i].fn)
			{
				matches[count] = SMB_STRDUP(commands->cmd_set[i].name);
				if (!matches[count])
					return NULL;
				count++;
			}
		}

		commands = commands->next;
	}

	if (count == 2) {
		SAFE_FREE(matches[0]);
		matches[0] = SMB_STRDUP(matches[1]);
	}
	matches[count] = NULL;
	return matches;
}

static char *next_command(TALLOC_CTX *ctx, char **cmdstr)
{
	char *command;
	char *p;

	if (!cmdstr || !(*cmdstr))
		return NULL;

	p = strchr_m(*cmdstr, ';');
	if (p)
		*p = '\0';
	command = talloc_strdup(ctx, *cmdstr);

	/* Pass back the remaining cmdstring 
	   (a trailing delimiter ";" does also work),
	   or NULL at last cmdstring.
	*/
	*cmdstr = p ? p + 1 : p;

	return command;
}

/* Load specified configuration file */
static NTSTATUS cmd_conf(struct vfs_state *vfs, TALLOC_CTX *mem_ctx,
			int argc, const char **argv)
{
	if (argc != 2) {
		printf("Usage: %s <smb.conf>\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (!lp_load_with_shares(argv[1])) {
		printf("Error loading \"%s\"\n", argv[1]);
		return NT_STATUS_OK;
	}

	printf("\"%s\" successfully loaded\n", argv[1]);
	return NT_STATUS_OK;
}

/* Display help on commands */
static NTSTATUS cmd_help(struct vfs_state *vfs, TALLOC_CTX *mem_ctx,
			 int argc, const char **argv)
{
	struct cmd_list *tmp;
	struct cmd_set *tmp_set;

	/* Usage */
	if (argc > 2) {
		printf("Usage: %s [command]\n", argv[0]);
		return NT_STATUS_OK;
	}

	/* Help on one command */

	if (argc == 2) {
		for (tmp = cmd_list; tmp; tmp = tmp->next) {

			tmp_set = tmp->cmd_set;

			while(tmp_set->name) {
				if (strequal(argv[1], tmp_set->name)) {
					if (tmp_set->usage &&
					    tmp_set->usage[0])
						printf("%s\n", tmp_set->usage);
					else
						printf("No help for %s\n", tmp_set->name);

					return NT_STATUS_OK;
				}

				tmp_set++;
			}
		}

		printf("No such command: %s\n", argv[1]);
		return NT_STATUS_OK;
	}

	/* List all commands */

	for (tmp = cmd_list; tmp; tmp = tmp->next) {

		tmp_set = tmp->cmd_set;

		while(tmp_set->name) {

			printf("%15s\t\t%s\n", tmp_set->name,
			       tmp_set->description ? tmp_set->description:
			       "");

			tmp_set++;
		}
	}

	return NT_STATUS_OK;
}

/* Change the debug level */
static NTSTATUS cmd_debuglevel(struct vfs_state *vfs, TALLOC_CTX *mem_ctx, int argc, const char **argv)
{
	if (argc > 2) {
		printf("Usage: %s [debuglevel]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 2) {
		struct loadparm_context *lp_ctx = samba_cmdline_get_lp_ctx();
		lpcfg_set_cmdline(lp_ctx, "log level", argv[1]);
	}

	printf("debuglevel is %d\n", DEBUGLEVEL);

	return NT_STATUS_OK;
}

static NTSTATUS cmd_freemem(struct vfs_state *vfs, TALLOC_CTX *mem_ctx, int argc, const char **argv)
{
	/* Cleanup */
	talloc_destroy(mem_ctx);
	mem_ctx = NULL;
	vfs->data = NULL;
	vfs->data_size = 0;
	return NT_STATUS_OK;
}

static NTSTATUS cmd_quit(struct vfs_state *vfs, TALLOC_CTX *mem_ctx, int argc, const char **argv)
{
	/* Cleanup */
	talloc_destroy(mem_ctx);

	exit(0);
	return NT_STATUS_OK; /* NOTREACHED */
}

static struct cmd_set vfstest_commands[] = {

	{ .name = "GENERAL OPTIONS" },

	{ "conf", 	cmd_conf, 	"Load smb configuration file", "conf <smb.conf>" },
	{ "help", 	cmd_help, 	"Get help on commands", "" },
	{ "?", 		cmd_help, 	"Get help on commands", "" },
	{ "debuglevel", cmd_debuglevel, "Set debug level", "" },
	{ "freemem",	cmd_freemem,	"Free currently allocated buffers", "" },
	{ "exit", 	cmd_quit, 	"Exit program", "" },
	{ "quit", 	cmd_quit, 	"Exit program", "" },

	{ .name = NULL }
};

static struct cmd_set separator_command[] = {
	{
		.name        = "---------------",
		.description = "----------------------"
	},
	{
		.name = NULL,
	},
};


extern struct cmd_set vfs_commands[];
static struct cmd_set *vfstest_command_list[] = {
	vfstest_commands,
	vfs_commands,
	NULL
};

static void add_command_set(struct cmd_set *cmd_set)
{
	struct cmd_list *entry;

	if (!(entry = SMB_MALLOC_P(struct cmd_list))) {
		DEBUG(0, ("out of memory\n"));
		return;
	}

	ZERO_STRUCTP(entry);

	entry->cmd_set = cmd_set;
	DLIST_ADD(cmd_list, entry);
}

static NTSTATUS do_cmd(struct vfs_state *vfs, struct cmd_set *cmd_entry, char *cmd)
{
	const char *p = cmd;
	const char **argv = NULL;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	char *buf;
	TALLOC_CTX *mem_ctx = talloc_stackframe();
	int argc = 0;

	/* Count number of arguments first time through the loop then
	   allocate memory and strdup them. */

 again:
	while(next_token_talloc(mem_ctx, &p, &buf, " ")) {
		if (argv) {
			argv[argc] = talloc_strdup(argv, buf);
		}
		argc++;
	}

	if (!argv) {
		/* Create argument list */

		argv = talloc_zero_array(mem_ctx, const char *, argc);
		if (argv == NULL) {
			fprintf(stderr, "out of memory\n");
			result = NT_STATUS_NO_MEMORY;
			goto done;
		}

		p = cmd;
		argc = 0;

		goto again;
	}

	/* Call the function */

	if (cmd_entry->fn) {
		/* Run command */
		result = cmd_entry->fn(vfs, mem_ctx, argc, (const char **)argv);
	} else {
		fprintf (stderr, "Invalid command\n");
		goto done;
	}

 done:

	/* Cleanup */

	if (argv) {
		char **_argv = discard_const_p(char *, argv);
		TALLOC_FREE(_argv);
		argv = NULL;
	}

	if (memreports != 0) {
		talloc_report_full(mem_ctx, stdout);
	}
	TALLOC_FREE(mem_ctx);
	return result;
}

/* Process a command entered at the prompt or as part of -c */
static NTSTATUS process_cmd(struct vfs_state *vfs, char *cmd)
{
	struct cmd_list *temp_list;
	bool found = False;
	char *buf;
	const char *p = cmd;
	NTSTATUS result = NT_STATUS_OK;
	TALLOC_CTX *mem_ctx = talloc_stackframe();
	int len = 0;

	if (cmd[strlen(cmd) - 1] == '\n')
		cmd[strlen(cmd) - 1] = '\0';

	if (!next_token_talloc(mem_ctx, &p, &buf, " ")) {
		TALLOC_FREE(mem_ctx);
		return NT_STATUS_OK;
	}

	/* Strip the trailing \n if it exists */
	len = strlen(buf);
	if (buf[len-1] == '\n')
		buf[len-1] = '\0';

	/* Search for matching commands */

	for (temp_list = cmd_list; temp_list; temp_list = temp_list->next) {
		struct cmd_set *temp_set = temp_list->cmd_set;

		while(temp_set->name) {
			if (strequal(buf, temp_set->name)) {
				found = True;
				result = do_cmd(vfs, temp_set, cmd);

				goto done;
			}
			temp_set++;
		}
	}

 done:
	if (!found && buf[0]) {
		printf("command not found: %s\n", buf);
		TALLOC_FREE(mem_ctx);
		return NT_STATUS_OK;
	}

	if (!NT_STATUS_IS_OK(result)) {
		printf("result was %s\n", nt_errstr(result));
	}

	TALLOC_FREE(mem_ctx);
	return result;
}

static void process_file(struct vfs_state *pvfs, char *filename) {
	FILE *file;
	char command[3 * PATH_MAX];

	if (*filename == '-') {
		file = stdin;
	} else {
		file = fopen(filename, "r");
		if (file == NULL) {
			printf("vfstest: error reading file (%s)!", filename);
			printf("errno n.%d: %s", errno, strerror(errno));
			exit(-1);
		}
	}

	while (fgets(command, 3 * PATH_MAX, file) != NULL) {
		process_cmd(pvfs, command);
	}

	if (file != stdin) {
		fclose(file);
	}
}

static void vfstest_exit_server(const char * const reason) _NORETURN_;
static void vfstest_exit_server(const char * const reason)
{
	DEBUG(3,("Server exit (%s)\n", (reason ? reason : "")));
	exit(0);
}

static void vfstest_exit_server_cleanly(const char * const reason) _NORETURN_;
static void vfstest_exit_server_cleanly(const char * const reason)
{
	vfstest_exit_server("normal exit");
}

struct smb_request *vfstest_get_smbreq(TALLOC_CTX *mem_ctx,
				       struct vfs_state *vfs)
{
	struct smb_request *result;
	uint8_t *inbuf;

	result = talloc_zero(mem_ctx, struct smb_request);
	if (result == NULL) {
		return NULL;
	}
	result->sconn = vfs->conn->sconn;
	result->mid = ++vfs->mid;

	inbuf = talloc_array(result, uint8_t, smb_size);
	if (inbuf == NULL) {
		goto fail;
	}
	SSVAL(inbuf, smb_mid, result->mid);
	smb_setlen(inbuf, smb_size-4);
	result->inbuf = inbuf;
	return result;
fail:
	TALLOC_FREE(result);
	return NULL;
}

/* Main function */

int main(int argc, const char *argv[])
{
	char *cmdstr = NULL;
	struct cmd_set	**cmd_set;
	struct conn_struct_tos *c = NULL;
	struct vfs_state *vfs;
	int opt;
	int i;
	char *filename = NULL;
	char *cwd = NULL;
	TALLOC_CTX *frame = talloc_stackframe();
	struct auth_session_info *session_info = NULL;
	NTSTATUS status = NT_STATUS_OK;
	bool ok;

	/* make sure the vars that get altered (4th field) are in
	   a fixed location or certain compilers complain */
	poptContext pc;
	struct poptOption long_options[] = {
		POPT_AUTOHELP
		{
			.longName   = "file",
			.shortName  = 'f',
			.argInfo    = POPT_ARG_STRING,
			.arg        = &filename,
		},
		{
			.longName   = "command",
			.shortName  = 'c',
			.argInfo    = POPT_ARG_STRING,
			.arg        = &cmdstr,
			.val        = 0,
			.descrip    = "Execute specified list of commands",
		},
		{
			.longName   = "memreport",
			.shortName  = 'm',
			.argInfo    = POPT_ARG_INT,
			.arg        = &memreports,
			.descrip    = "Report memory left on talloc stackframe after each command",
		},
		POPT_COMMON_SAMBA
		POPT_COMMON_VERSION
		POPT_TABLEEND
	};
	static const struct smbd_shim vfstest_shim_fns =
	{
		.exit_server = vfstest_exit_server,
		.exit_server_cleanly = vfstest_exit_server_cleanly,
	};

	smb_init_locale();

	setlinebuf(stdout);

	ok = samba_cmdline_init(frame,
				SAMBA_CMDLINE_CONFIG_SERVER,
				true /* require_smbconf */);
	if (!ok) {
		TALLOC_FREE(frame);
		exit(1);
	}

	pc = samba_popt_get_context("vfstest", argc, argv, long_options, 0);
	if (pc == NULL) {
		TALLOC_FREE(frame);
		exit(1);
	}

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case POPT_ERROR_BADOPT:
			fprintf(stderr, "\nInvalid option %s: %s\n\n",
				poptBadOption(pc, 0), poptStrerror(opt));
			poptPrintUsage(pc, stderr, 0);
			exit(1);
		}
	}

	poptFreeContext(pc);

	/* we want total control over the permissions on created files,
	   so set our umask to 0 */
	umask(0);

	/* TODO: check output */
	reload_services(NULL, NULL, false);

	per_thread_cwd_check();

	set_smbd_shim(&vfstest_shim_fns);

	/* Load command lists */

	cmd_set = vfstest_command_list;

	while(*cmd_set) {
		add_command_set(*cmd_set);
		add_command_set(separator_command);
		cmd_set++;
	}

	/* some basic initialization stuff */
	sec_init();
	init_guest_session_info(frame);
	locking_init();
	vfs = talloc_zero(frame, struct vfs_state);
	if (vfs == NULL) {
		return 1;
	}
	status = make_session_info_guest(vfs, &session_info);
	if (!NT_STATUS_IS_OK(status)) {
		return 1;
	}

	/* Provided by libreplace if not present. Always mallocs. */
	cwd = get_current_dir_name();
	if (cwd == NULL) {
		return -1;
	}

	status = create_conn_struct_tos_cwd(global_messaging_context(),
					-1,
					cwd,
					session_info,
					&c);
	SAFE_FREE(cwd);
	if (!NT_STATUS_IS_OK(status)) {
		return 1;
	}
	vfs->conn = c->conn;

	vfs->conn->share_access = FILE_GENERIC_ALL;
	vfs->conn->read_only = false;

	file_init(vfs->conn->sconn);
	for (i=0; i < 1024; i++)
		vfs->files[i] = NULL;

	if (!posix_locking_init(false)) {
		return 1;
	}

	/* Do we have a file input? */
	if (filename && filename[0]) {
		process_file(vfs, filename);
		return 0;
	}

	/* Do anything specified with -c */
	if (cmdstr && cmdstr[0]) {
		char    *cmd;
		char    *p = cmdstr;

		while((cmd=next_command(frame, &p)) != NULL) {
			status = process_cmd(vfs, cmd);
		}

		TALLOC_FREE(cmd);
		return NT_STATUS_IS_OK(status) ? 0 : 1;
	}

	/* Loop around accepting commands */

	while(1) {
		char *line = NULL;

		line = smb_readline("vfstest $> ", NULL, completion_fn);

		if (line == NULL) {
			break;
		}

		if (line[0] != '\n') {
			status = process_cmd(vfs, line);
		}
		SAFE_FREE(line);
	}

	TALLOC_FREE(vfs);
	TALLOC_FREE(frame);
	return NT_STATUS_IS_OK(status) ? 0 : 1;
}
