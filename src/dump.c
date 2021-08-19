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
usage_dump(void)
{
	fprintf(stderr, "Usage: pkg dump [-f output] ...\n");
	fprintf(stderr, "       pkg dump -f output.json\n\n");
	fprintf(stderr, "For more information see 'pkg help dump'.\n");
}

int
exec_dump(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg      *pkg = NULL;
	ucl_object_t    *obj = NULL;
	unsigned char   *result = NULL;
	FILE            *output = stdout;
	char            *pattern = NULL;
	char             package[MAXPATHLEN] = { 0 };
	match_t          match = MATCH_ALL;
	int		 ch;
	int		 retcode = EXIT_FAILURE;
	int		 lock_type = PKGDB_LOCK_READONLY;

	struct option longopts[] = {
		{ "output",			no_argument,	NULL,	'f' },
		{ NULL,				0,		NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "f:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			output = fopen(optarg, "w");
			if (output == NULL) {
				fprintf(stdout, "Failed to open file for writing\n");
				return (EXIT_FAILURE);
			}

			break;
		default:
			usage_dump();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);

	if (retcode == EPKG_ENODB) {
		warnx("No packages installed.  Nothing to do!");
		if (output != stdout)
			fclose(output);

		return (EXIT_SUCCESS);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to delete packages");
		if (output != stdout)
			fclose(output);

		return (EXIT_FAILURE);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		if (output != stdout)
			fclose(output);

		return (EXIT_FAILURE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		if (output != stdout)
			fclose(output);

		return (EXIT_FAILURE);
	}

	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		if (output != stdout)
			fclose(output);

		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	if ((it = pkgdb_query(db, pattern, match)) == NULL) {
		goto cleanup;
	}

	obj = ucl_object_typed_new(UCL_ARRAY);
	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		ucl_object_t *tmp = NULL;
		pkg_snprintf(package, sizeof(package), "%n-%v", pkg, pkg);

		tmp = ucl_object_fromstring_common(package, 0, UCL_STRING_RAW);
		ucl_array_append(obj, tmp);
	}

	result = ucl_object_emit(obj, UCL_EMIT_JSON);
	fprintf(output, "%s\n", result);
	free(result);

cleanup:
	if (obj) {
		ucl_object_unref(obj);
	}

	pkgdb_release_lock(db, lock_type);
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	if (output != stdout) {
		fsync(fileno(output));
		fclose(output);
	}

	return (retcode);
}

FILE *
output_open(const char *output_filename) {
	FILE *out;

	out = fopen(output_filename, "w");
	if (out == NULL) {
		fprintf(stderr, "Failed to open file for writing\n");
		exit(7);
	}

	return out;
}

void
output_close(FILE *out) {
	int success;

	success = fsync(fileno(out));
	if (success != 0) {
		fprintf(stderr, "Failed to sync file\n");
		exit(7);
	}

	success = fclose(out);
	if (success != 0) {
		fprintf(stderr, "Failed to close file\n");
		exit(7);
	}

	out = NULL;
}
