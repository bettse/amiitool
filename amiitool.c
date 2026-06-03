/*
 * (c) 2015-2017 Marcos Del Sol Vives
 * (c) 2016      javiMaD
 *
 * SPDX-License-Identifier: MIT
 */

#include <nfc3d/amiibo.h>
#include <nfc3d/version.h>
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#define NTAG215_SIZE 540
#define NTAG_I2C_2K_SIZE 2048

// NTAG I2C Plus 2K ("v3", e.g. Kirby Air Riders) dumps are much larger than a
// 540-byte NTAG215 dump. The Flipper-derived variant is 1968 bytes and the full
// sector 0+1 image is 2048; both sit well above this threshold, while the
// 944-byte NTAG I2C Plus 1K dump stays below it.
#define TAG_V3_MIN_SIZE 1024

static char * self;

void usage() {
	fprintf(stderr,
		"amiitool build %i (commit %s-%08x)\n"
		"by Marcos Del Sol Vives <marcos@dracon.es>\n"
		"\n"
		"Usage: %s (-e|-d|-c) -k keyfile [-i input] [-s input2] [-o output]\n"
		"   -e encrypt and sign amiibo\n"
		"   -d decrypt and test amiibo\n"
		"   -c decrypt, copy AppData and encrypt amiibo\n"
		"   -k key set file. For retail amiibo, use \"retail unfixed\" key set\n"
		"   -i input file. If not specified, stdin will be used.\n"
		"   -s input save file, save from this file will replace input file ones.\n"
		"   -o output file. If not specified, stdout will be used.\n"
		"   -l decrypt files with invalid signatures.\n",
		nfc3d_version_build(), nfc3d_version_fork(), nfc3d_version_commit(), self
	);
}

int main(int argc, char ** argv) {
	self = argv[0];

	char * infile = NULL;
	char * savefile = NULL;
	char * outfile = NULL;
	char * keyfile = NULL;
	char op = '\0';
	bool lenient = false;

	char c;
	while ((c = getopt(argc, argv, "edci:s:o:k:l")) != -1) {
		switch (c) {
			case 'e':
			case 'd':
			case 'c':
				op = c;
				break;
			case 'i':
				infile = optarg;
				break;
			case 's':
				savefile = optarg;
				break;
			case 'o':
				outfile = optarg;
				break;
			case 'k':
				keyfile = optarg;
				break;
			case 'l':
				lenient = true;
				break;
			default:
				usage();
				return 2;
		}
	}

	if (op == '\0' || keyfile == NULL) {
		usage();
		return 1;
	}

	nfc3d_amiibo_keys amiiboKeys;
	if (!nfc3d_amiibo_load_keys(&amiiboKeys, keyfile)) {
		fprintf(stderr, "Could not load keys from \"%s\": %s (%d)\n", keyfile, strerror(errno), errno);
		return 5;
	}

	uint8_t original[NTAG_I2C_2K_SIZE];
	uint8_t modified[NTAG_I2C_2K_SIZE];

	FILE * f = stdin;
	if (infile) {
		f = fopen(infile, "rb");
		if (!f) {
			fprintf(stderr, "Could not open input file: %s (%d)\n", strerror(errno), errno);
			return 3;
		}
	}
	size_t readPages = fread(original, 4, NTAG_I2C_2K_SIZE / 4, f);
	if (readPages < NFC3D_AMIIBO_SIZE / 4) {
		fprintf(stderr, "Could not read from input: %s (%d)\n", strerror(errno), errno);
		return 3;
	}
	fclose(f);

	size_t readBytes = readPages * 4;
	bool tag_v3 = readBytes >= TAG_V3_MIN_SIZE;

	if (op == 'e') {
		if (tag_v3) {
			// The decrypted v3 image keeps the on-tag layout. Preserve the
			// whole tag (random block, config, SRAM, trailing) and let pack
			// overwrite only the encrypted amiibo regions.
			uint8_t plain[NFC3D_AMIIBO_SIZE];
			nfc3d_amiibo_tag_to_internal(original, plain, true);
			memcpy(modified, original, readBytes);
			nfc3d_amiibo_pack(&amiiboKeys, plain, modified, true);
		} else {
			nfc3d_amiibo_pack(&amiiboKeys, original, modified, false);
			memcpy(modified + NFC3D_AMIIBO_SIZE, original + NFC3D_AMIIBO_SIZE, readBytes - NFC3D_AMIIBO_SIZE);
		}
	} else if (op == 'd') {
		uint8_t plain[NFC3D_AMIIBO_SIZE];
		if (!nfc3d_amiibo_unpack(&amiiboKeys, original, plain, tag_v3)) {
			fprintf(stderr, "!!! WARNING !!!: Tag signature was NOT valid\n");
			if (!lenient) {
				return 6;
			}
		}
		if (tag_v3) {
			// Decrypt in place within the v3 tag layout, preserving everything
			// outside the encrypted amiibo regions.
			memcpy(modified, original, readBytes);
			nfc3d_amiibo_internal_to_tag(plain, modified, true);
		} else {
			memcpy(modified, plain, NFC3D_AMIIBO_SIZE);
			memcpy(modified + NFC3D_AMIIBO_SIZE, original + NFC3D_AMIIBO_SIZE, readBytes - NFC3D_AMIIBO_SIZE);
		}
	} else { /* copy */
		if (tag_v3) {
			fprintf(stderr, "Copy (-c) is not supported for NTAG I2C Plus 2K tags\n");
			return 7;
		}

		uint8_t plain_base[NFC3D_AMIIBO_SIZE];
		uint8_t plain_save[NFC3D_AMIIBO_SIZE];

		if (!nfc3d_amiibo_unpack(&amiiboKeys, original, plain_base, false)) {
			fprintf(stderr, "!!! WARNING !!!: Tag signature was NOT valid\n");
			if (!lenient) {
				return 6;
			}
		}
		if (savefile) {
			f = fopen(savefile, "rb");
			if (!f) {
				fprintf(stderr, "Could not open save file: %s (%d)\n", strerror(errno), errno);
				return 3;
			}
		}
		readPages = fread(original, 4, NTAG_I2C_2K_SIZE / 4, f);
		if (readPages < NFC3D_AMIIBO_SIZE / 4) {
			fprintf(stderr, "Could not read from save: %s (%d)\n", strerror(errno), errno);
			return 3;
		}
		fclose(f);
		readBytes = readPages * 4;

		if (!nfc3d_amiibo_unpack(&amiiboKeys, original, plain_save, false)) {
			fprintf(stderr, "!!! WARNING !!!: Tag signature was NOT valid\n");
			if (!lenient) {
				return 6;
			}
		}

		nfc3d_amiibo_copy_app_data(plain_save, plain_base);
		nfc3d_amiibo_pack(&amiiboKeys, plain_base, modified, false);
		memcpy(modified + NFC3D_AMIIBO_SIZE, original + NFC3D_AMIIBO_SIZE, readBytes - NFC3D_AMIIBO_SIZE);
	}

	f = stdout;
	if (outfile) {
		f = fopen(outfile, "wb");
		if (!f) {
			fprintf(stderr, "Could not open output file: %s (%d)\n", strerror(errno), errno);
			return 4;
		}
	}
	if (fwrite(modified, readBytes, 1, f) != 1) {
		fprintf(stderr, "Could not write to output: %s (%d)\n", strerror(errno), errno);
		return 4;
	}
	fclose(f);

	return 0;
}
