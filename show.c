/*-------------------------------------------------------------------------
 *
 * show.c: show backup catalog.
 *
 * Copyright (c) 2009-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#include "pg_arman.h"

static void show_backup_list(FILE *out, parray *backup_list, bool show_all);
static void show_backup_detail(FILE *out, pgBackup *backup);

/*
 * Show backup catalog information.
 * If range is { 0, 0 }, show list of all backup, otherwise show detail of the
 * backup indicated by id.
 */
int
do_show(pgBackupRange *range, bool show_all)
{
	/*
	 * Safety check for archive folder, this is necessary to fetch
	 * the parent TLI from history field generated by server after
	 * child timeline is chosen.
	 */
	if (arclog_path == NULL)
		elog(ERROR_ARGS,
			 _("required parameter not specified: ARCLOG_PATH (-A, --arclog-path)"));

	if (pgBackupRangeIsSingle(range))
	{
		pgBackup *backup;

		backup = catalog_get_backup(range->begin);
		if (backup == NULL)
		{
			char timestamp[100];
			time2iso(timestamp, lengthof(timestamp), range->begin);
			elog(INFO, _("backup taken at \"%s\" does not exist."),
				timestamp);
			/* This is not error case */
			return 0;
		}
		show_backup_detail(stdout, backup);

		/* cleanup */
		pgBackupFree(backup);
	}
	else
	{
		parray *backup_list;

		backup_list = catalog_get_backup_list(range);
		if (backup_list == NULL){
			elog(ERROR_SYSTEM, _("can't process any more."));
		}

		show_backup_list(stdout, backup_list, show_all);

		/* cleanup */
		parray_walk(backup_list, pgBackupFree);
		parray_free(backup_list);
	}

	return 0;
}

static void
pretty_size(int64 size, char *buf, size_t len)
{
	int exp = 0;

	/* minus means the size is invalid */
	if (size < 0)
	{
		strncpy(buf, "----", len);
		return;
	}

	/* determine postfix */
	while (size > 9999)
	{
		++exp;
		size /= 1000;
	}

	switch (exp)
	{
		case 0:
			snprintf(buf, len, INT64_FORMAT "B", size);
			break;
		case 1:
			snprintf(buf, len, INT64_FORMAT "kB", size);
			break;
		case 2:
			snprintf(buf, len, INT64_FORMAT "MB", size);
			break;
		case 3:
			snprintf(buf, len, INT64_FORMAT "GB", size);
			break;
		case 4:
			snprintf(buf, len, INT64_FORMAT "TB", size);
			break;
		case 5:
			snprintf(buf, len, INT64_FORMAT "PB", size);
			break;
		default:
			strncpy(buf, "***", len);
			break;
	}
}

static TimeLineID
get_parent_tli(TimeLineID child_tli)
{
	TimeLineID	result = 0;
	char		path[MAXPGPATH];
	char		fline[MAXPGPATH];
	FILE	   *fd;

	/* Search history file in archives */
	snprintf(path, lengthof(path), "%s/%08X.history", arclog_path,
		child_tli);
	fd = fopen(path, "rt");
	if (fd == NULL)
	{
		if (errno != ENOENT)
			elog(ERROR_SYSTEM, _("could not open file \"%s\": %s"), path,
				strerror(errno));

		return 0;
	}

	/*
	 * Parse the file...
	 */
	while (fgets(fline, sizeof(fline), fd) != NULL)
	{
		/* skip leading whitespace and check for # comment */
		char	   *ptr;
		char	   *endptr;

		for (ptr = fline; *ptr; ptr++)
		{
			if (!IsSpace(*ptr))
				break;
		}
		if (*ptr == '\0' || *ptr == '#')
			continue;

		/* expect a numeric timeline ID as first field of line */
		result = (TimeLineID) strtoul(ptr, &endptr, 0);
		if (endptr == ptr)
			elog(ERROR_CORRUPTED,
					_("syntax error(timeline ID) in history file: %s"),
					fline);
	}

	fclose(fd);

	/* TLI of the last line is parent TLI */
	return result;
}

static void
show_backup_list(FILE *out, parray *backup_list, bool show_all)
{
	int i;

	/* show header */
	fputs("==========================================================================\n", out);
	fputs("Start                Mode  Current TLI  Parent TLI  Time    Data  Status  \n", out);
	fputs("==========================================================================\n", out);

	for (i = 0; i < parray_num(backup_list); i++)
	{
		pgBackup *backup;
		const char *modes[] = { "", "PAGE", "FULL"};
		TimeLineID  parent_tli;
		char timestamp[20];
		char duration[20] = "----";
		char data_bytes_str[10] = "----";

		backup = parray_get(backup_list, i);

		/* skip deleted backup */
		if (backup->status == BACKUP_STATUS_DELETED && !show_all)
			continue;

		time2iso(timestamp, lengthof(timestamp), backup->start_time);
		if (backup->end_time != (time_t) 0)
			snprintf(duration, lengthof(duration), "%lum",
				(backup->end_time - backup->start_time) / 60);

		/*
		 * Calculate Data field, in the case of full backup this shows the
		 * total amount of data. For an differential backup, this size is only
		 * the difference of data accumulated.
		 */
		pretty_size(backup->data_bytes, data_bytes_str,
				lengthof(data_bytes_str));

		/* Get parent timeline before printing */
		parent_tli = get_parent_tli(backup->tli);

		fprintf(out, "%-19s  %-4s   %10d  %10d %5s  %6s  %s \n",
				timestamp,
				modes[backup->backup_mode],
				backup->tli,
				parent_tli,
				duration,
				data_bytes_str,
				status2str(backup->status));
	}
}

static void
show_backup_detail(FILE *out, pgBackup *backup)
{
	pgBackupWriteConfigSection(out, backup);
	pgBackupWriteResultSection(out, backup);
}
