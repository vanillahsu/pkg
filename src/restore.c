/*-
 * Copyright (c) 2021 San-Tai Hsu <vanilla@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include <ucl.h>

#include "pkgcli.h"

void
usage_restore(void)
{
	fprintf(stderr, "Usage: pkg restore [-f input] ...\n");
	fprintf(stderr, "       pkg restore -f input.json\n\n");
	fprintf(stderr, "       pkg restore [-nf]\n\n");
	fprintf(stderr, "For more information see 'pkg help restore'.\n");
}

int
exec_restore(int argc, char **argv)
{
	struct pkgdb       *db = NULL;
	struct pkg_jobs    *jobs = NULL;
	struct ucl_parser  *parser = NULL;
	const char	   *output = NULL;
	const char         *reponame = NULL;
	const ucl_object_t *cur;
	ucl_object_t	   *obj = NULL;
	ucl_object_iter_t   it = NULL;
	int		    ch, mode, nargc = 0, i = 0, done = 0;
	int		    retcode = EXIT_FAILURE, repo_type;
	int		    lock_type = PKGDB_LOCK_ADVISORY;
	bool                rc = true;
	bool                local_only = false;
	match_t             match = MATCH_GLOB;
	pkg_flags	    f = PKG_FLAG_NONE | PKG_FLAG_PKG_VERSION_TEST;
	char              **nargv;

	struct option longopts[] = {
		{ "output",			required_argument,	NULL,	'f' },
		{ "repository",                 required_argument,      NULL,   'r' },
		{ "dry-run",                    no_argument,		NULL,	'n' },
		{ "quiet",                      no_argument,            NULL,   'q' },
		{ NULL,				0,			NULL,	0   },
	};

	nbactions = nbdone = 0;

	while ((ch = getopt_long(argc, argv, "nf:r:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			output = optarg;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			lock_type = PKGDB_LOCK_READONLY;
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		default:
			usage_restore();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (output == NULL) {
		usage_restore();
		return (EXIT_FAILURE);
	}

	if (dry_run)
		mode = PKGDB_MODE_READ;
	else
		mode = PKGDB_MODE_READ | PKGDB_MODE_WRITE | PKGDB_MODE_CREATE;

	if (local_only)
		repo_type = PKGDB_DB_LOCAL;
	else
		repo_type = PKGDB_DB_LOCAL | PKGDB_DB_REPO;

	retcode = pkgdb_access(mode, repo_type);

	if (retcode == EPKG_ENODB) {
		warnx("No packages installed.  Nothing to do!");
		return (EXIT_SUCCESS);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to delete packages");
		return (EXIT_FAILURE);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EXIT_FAILURE);
	}

	if (pkgdb_open_all(&db,
	    local_only ? PKGDB_DEFAULT : PKGDB_MAYBE_REMOTE,
	    reponame) != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK)
		goto cleanup;

	parser = ucl_parser_new(UCL_PARSER_DEFAULT);
	if (parser == NULL) {
		warnx("Failed to create parser");
		goto cleanup;
	}

	ucl_parser_add_file(parser, output);
	obj = ucl_parser_get_object(parser);
	nargc = ucl_array_size(obj);
	nargv = malloc(nargc + 1);

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		if (cur == NULL)
			continue;

		nargv[i] = strdup(ucl_object_tostring(cur));
		i++;
	}

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_add(jobs, match, nargv, nargc) == EPKG_FATAL)
		goto cleanup;

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	while ((nbactions = pkg_jobs_count(jobs)) > 0) {
		rc = yes;

		if (!quiet || dry_run) {
			print_jobs_summary(jobs,
				"The following %d package(s) will be affected (of %d checked):\n\n",
				nbactions, pkg_jobs_total(jobs));
			if (!dry_run) {
				rc = query_yesno(false,
					"\nProceed with this action? ");
			} else {
				rc = false;
			}
		}

		if (rc) {
			retcode = pkg_jobs_apply(jobs);
			done = 1;
			if (retcode == EPKG_CONFLICT) {
				printf("Conflicts with the existing packages "
					"have been found.\nOne more solver "
					"iteration is needed to resolve them.\n");
				continue;
			} else if (retcode != EPKG_OK)
				goto cleanup;
		}

		if (messages != NULL) {
			fflush(messages->fp);
			printf("%s", messages->buf);
		}
		break;
	}

	if (done == 0 && rc)
		printf("The most recent versions of packages are already installed\n");

	if (rc)
		retcode = EXIT_SUCCESS;
	else
		retcode = EXIT_FAILURE;

cleanup:
	if (obj)
		ucl_object_unref(obj);

	if (parser)
		ucl_parser_free(parser);

	pkgdb_release_lock(db, lock_type);
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	if (!dry_run)
		pkg_cache_full_clean();

	return (retcode);
}
