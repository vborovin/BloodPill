////////////////////////////////////////////////////////////////
//
// Bigfile operations
// coded by Pavel [VorteX] Timofeyev and placed to public domain
// thanks to XentaX (www.xentax.com) community for providing bigfile specs
//
////////////////////////////////

#include "bloodpill.h"
#include "bigfile.h"
#include "cmdlib.h"
#include "mem.h"

#define DEFAULT_BIGFILENAME	"pill.big"
#define DEFAULT_PACKPATH	"bigfile"

char bigfile[MAX_BLOODPATH];

// knowledge base
typedef struct
{
	unsigned int hash;
	int samplingrate;
	bigentrytype_t type;
}
bigkentry_t;

typedef struct
{
	int numentries;
	bigkentry_t *entries;
}
bigklist_t;

bigklist_t *bigklist;

/*
==========================================================================================

  KNOWN-FILES FEATURE

==========================================================================================
*/

bigentrytype_t BigfileTypeForExt(char *ext)
{
	int i, numbytes;

	Q_strlower(ext);
	numbytes = strlen(ext);

	// null-length ext?
	if (!numbytes)
		return BIGENTRY_UNKNOWN;

	// find type
	for (i = 0; i < BIGFILE_NUM_FILETYPES; i++)
		if (!memcmp(bigentryext[i], ext, numbytes))
			return i;

	return BIGENTRY_UNKNOWN;
}

bigklist_t *BigfileEmptyKList()
{
	bigklist_t *klist;

	klist = qmalloc(sizeof(bigklist_t));
	klist->entries = NULL;
	klist->numentries = 0;

	return klist;
}

bigklist_t *BigfileLoadKList(char *filename)
{
	bigklist_t *klist;
	int linenum = 0;
	char line[256], typestr[256];
	unsigned int hash;
	int parm1, parms, i;
	FILE *f;

	klist = BigfileEmptyKList();

	f = SafeOpen(filename, "r");
	// first pass - scan klist to determine how many string we should allocate
	while(!feof(f))
	{
		fgets(line, 1024, f);
		if (line[0] == '#')
			continue;
		linenum++;
	}

	// allocate
	klist->entries = qmalloc(linenum * sizeof(bigkentry_t));

	// seconds pass - parse klist
	fseek(f, 0, SEEK_SET);
	linenum = 0;
	while(!feof(f))
	{
		linenum++;
		fgets(line, 1024, f);
		if (line[0] == '#')
			continue;

		// scan
		parms = sscanf(line,"%X=%s %i", &hash, typestr, &parm1);
		if (parms < 2)
		{
			printf("%s: parse error on line %i: %s", filename, linenum, line);
			continue;
		}
		klist->entries[klist->numentries].hash = hash;
		klist->entries[klist->numentries].type = BigfileTypeForExt(typestr);

		// warn for double defienition
		for (i = 0; i < klist->numentries; i++)
			if (klist->entries[i].hash == hash)
				printf("warning: redefenition of hash %.8X on line %i\n", hash, linenum);

		// VAG - sampling rate
		if (klist->entries[klist->numentries].type == BIGENTRY_RAW_VAG)
			klist->entries[klist->numentries].samplingrate = (parms < 3) ? 11025 : parm1;

		// parsed
		klist->numentries++;
	}

	printf("loaded known-files list with %i entries\n", klist->numentries);

	return klist;
}

bigkentry_t *BigfileSearchKList(unsigned int hash)
{
	int i;

	for (i = 0; i < bigklist->numentries; i++)
		if (bigklist->entries[i].hash == hash)
			return &bigklist->entries[i];

	return NULL;
}

/*
==========================================================================================

  BigFile subs

==========================================================================================
*/

void BigfileEmptyEntry(bigfileentry_t *entry)
{
	entry->timdim = NULL;
	entry->data = NULL;
}

void BigfileSeekFile(FILE *f, bigfileentry_t *entry)
{
	if (fseek(f, (long int)entry->offset, SEEK_SET))
		Error( "error seeking for data on file %.8X", entry->hash);
}

void BigfileSeekContents(FILE *f, byte *contents, bigfileentry_t *entry)
{
	if (fseek(f, (long int)entry->offset, SEEK_SET))
		Error( "error seeking for data on file %.8X", entry->hash);

	if (fread(contents, entry->size, 1, f) < 1)
		Error( "error reading data on file %.8X (%s)", entry->hash, strerror(errno));
}

void BigFileUnpackFile(FILE *f, bigfileentry_t *entry, FILE *dstf)
{
	byte *contents;

	contents = (byte *)qmalloc(entry->size);
	BigfileSeekContents(f, contents, entry);
	fwrite(contents, 1, entry->size, dstf);
	qfree(contents);
}

void BigfileWriteListfile(FILE *f, bigfileheader_t *data)
{
	bigfileentry_t *entry;
	int i;

	if (f != stdout)
		fprintf(f, "numentries=%i\n", data->numentries);
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		// write general data
		fprintf(f, "[%.8X]\n", entry->hash);
		fprintf(f, "type=%i\n", (int)entry->type);
		fprintf(f, "size=%i\n", (int)entry->size);
		fprintf(f, "offset=%i\n", (int)entry->offset);
		fprintf(f, "file=%s\n", entry->name);

		// write specific data for TIM images
		if (entry->timdim)
		{
			fprintf(f, "tim.xskip=%i\n", entry->timdim->xskip);
			fprintf(f, "tim.yskip=%i\n", entry->timdim->yskip);
			fprintf(f, "tim.xsize=%i\n", entry->timdim->xsize);
			fprintf(f, "tim.ysize=%i\n", entry->timdim->ysize);
		}
	}
}

bigfileheader_t *ReadBigfileHeader(FILE *f, char *filename, qboolean loadfilecontents)
{	
	bigfileheader_t *data;
	bigfileentry_t *entry;
	unsigned int read[3];
	int i;

	data = qmalloc(sizeof(bigfileheader_t));

	// read header
	if (fread(&data->numentries, sizeof(unsigned int), 1, f) < 1)
		Error("BigfileHeader: wrong of broken file\n");
	if (!data->numentries)
		Error("BigfileHeader: funny entries count, perhaps file is broken\n");
	printf("%s: %i entries\n", filename, data->numentries);

	// read entries
	data->entries = qmalloc(data->numentries * sizeof(bigfileentry_t));
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		BigfileEmptyEntry(entry);

		printf("\rreading entry %i of %i", i + 1, data->numentries);
		fflush(stdout);

		if (fread(&read, 12, 1, f) < 1)
			Error("BigfileHeader: error on entry %i (%s)\n", i, strerror(errno));
		entry->hash = read[0];
		entry->size = read[1];
		entry->offset = read[2];
		entry->type = BIGENTRY_UNKNOWN;
		sprintf(entry->name, "%.8X.%s", read[0], bigentryext[BIGENTRY_UNKNOWN]);
		if (!entry->hash || !entry->offset)
			Error("BigfileHeader: entry %i is broken\n", i);
	}
	printf("\n");

	// load contents
	if (loadfilecontents)
	{
		for (i = 0; i < (int)data->numentries; i++)
		{
			entry = &data->entries[i];
			if (entry->size <= 0)
				continue;

			printf("\rloading entry %i of %i", i + 1, data->numentries);
			fflush(stdout);

			entry->data = qmalloc(entry->size);
			BigfileSeekContents(f, entry->data, entry);
		}
		printf("\n");
	}

	// warnings
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		if (entry->size <= 0)
			printf("warning: entry %.8X size = %i bytes\n", entry->hash, entry->size);
	}

	return data;
}

// recalculate all file offsets
void BigfileHeaderRecalcOffsets(bigfileheader_t *data)
{
	bigfileentry_t *entry;
	int i, offset;

	offset = sizeof(unsigned int) + data->numentries*12;
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		entry->offset = (unsigned int)offset;
		offset = offset + entry->size;
	}
}

// check & fix entry that was loaded from listfile
void BigfileFixListfileEntry(char *srcdir, bigfileentry_t *entry)
{
	char ext[16], filename[MAX_BLOODPATH];
	FILE *f;

	// check extension
	ExtractFileExtension(entry->name, ext);
	if (strcmp(ext, bigentryext[entry->type]) != 0)
		Error("%s - packing conversion not yet supported", entry->name);
	
	// try open file, update filesize
	sprintf(filename, "%s/%s", srcdir, entry->name);
	f = SafeOpen(filename, "rb");
	entry->size = (unsigned int)Q_filelength(f);
	fclose(f);
}


// read bigfile header from listfile
bigfileheader_t *BigfileOpenListfile(char *srcdir)
{
	bigfileheader_t *data;
	bigfileentry_t *entry;
	char line[256], temp[128], filename[MAX_BLOODPATH];
	int numentries, linenum, val;
	FILE *f;

	// open file
	sprintf(filename, "%s/listfile.txt", srcdir);
	f = SafeOpen(filename, "r");

	// read number of entries
	data = qmalloc(sizeof(bigfileheader_t));
	if (fscanf(f, "numentries=%i\n", &numentries) != 1)
		Error("broken numentries record");
	printf("%s: %i entries\n", filename, numentries);

	// read all entries
	linenum = 1;
	entry = NULL;
	data->entries = qmalloc(numentries * sizeof(bigfileentry_t));
	data->numentries = 0;
	while(!feof(f))
	{
		linenum++;
		fgets(line, 256, f);

		// new entry
		if (line[0] == '[')
		{
			if (sscanf(line, "[%X]", &val) < 1)
				Error("bad entry definition on line %i: %s\n", linenum, line);

			// check old entry
			if (entry != NULL)
				BigfileFixListfileEntry(srcdir, entry);

			if ((int)data->numentries >= numentries)
				Error("entries overflow, numentries is out of date\n");

			entry = &data->entries[data->numentries];
			BigfileEmptyEntry(entry);
			entry->hash = (unsigned int)val;
			data->numentries++;
			
			printf("\rreading entry %i of %i", data->numentries, numentries);
			fflush(stdout);
			continue;
		}

		// scan parameter
		if (entry == NULL)
			Error("Entry data without actual entry on line %i: %s\n", linenum, line);

		// parse it
		if (sscanf(line, "type=%i", &val))
			entry->type = (bigentrytype_t)val;
		else if (sscanf(line, "size=%i", &val))
			entry->size = (int)val;
		else if (sscanf(line, "offset=%i", &val))
			entry->offset = (int)val;
		else if (sscanf(line, "file=%s", &temp))
			strcpy(entry->name, temp);

		// otherwise ignore it
	}

	// check last entry
	if (entry != NULL)
		BigfileFixListfileEntry(srcdir, entry);

	printf("\n");
	fclose(f);

	// recalc offsets
	BigfileHeaderRecalcOffsets(data);

	return data;
}


/*
==========================================================================================

  BigFile filetype scanner

==========================================================================================
*/

qboolean BigFileScanTIM(FILE *f, bigfileentry_t *entry, unsigned int type)
{
	tim_image_t *tim;
	unsigned int tag;
	unsigned int bpp;

	BigfileSeekFile(f, entry);
	// 0x10 should be at beginning of standart TIM
	if (fread(&tag, sizeof(unsigned int), 1, f) < 1)
		return false;
	if (tag != 0x10)
		return false;
	// second uint is BPP
	// todo: there are files with TIM header but with nasty BPP
	if (fread(&bpp, sizeof(unsigned int), 1, f) < 1)
		return false;
	if (bpp != type)
		return false;
	// try load that TIM
	BigfileSeekFile(f, entry);
	tim = TIM_LoadFromStream(f, entry->size);
	if (tim->error)
	{
		FreeTIM(tim);
		return false;
	}
	// fill diminfo section
	entry->timdim = qmalloc(sizeof(tim_diminfo_t));
	memcpy(entry->timdim, &tim->dim, sizeof(tim_diminfo_t));
	FreeTIM(tim);
	return true;
}

qboolean BigFileScanRiffWave(FILE *f, bigfileentry_t *entry)
{
	byte tag[4];

	BigfileSeekFile(f, entry);
	// first unsigned int - tag
	if (fread(&tag, sizeof(char), 4, f) < 1)
		return false;
	if (tag[0] != 0x52 || tag[1] != 0x49 || tag[2] != 0x46 || tag[3] != 0x46)
		return false;
	// it's a RIFF
	return true;
}

void BigfileScanFiletypes(FILE *f,bigfileheader_t *data)
{
	fpos_t fpos;
	bigfileentry_t *entry;
	bigkentry_t *kentry;
	int stats[BIGFILE_NUM_FILETYPES];
	int i;

	memset(stats, 0, sizeof(int)*BIGFILE_NUM_FILETYPES);
	fgetpos(f, &fpos);
	// scan for filetypes
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		printf("\rscanning type for entry %i of %i", i + 1, data->numentries);
		fflush(stdout);
		
		// scan for certain filetype
		kentry = BigfileSearchKList(entry->hash); // check for known filetype
		if (kentry != NULL)
		{
			entry->type = kentry->type;
			entry->samplingrate = kentry->samplingrate;
		}
		else if (BigFileScanTIM(f, entry, TIM_4Bit))
			entry->type = BIGENTRY_TIM4;
		else if (BigFileScanTIM(f, entry, TIM_8Bit))
			entry->type = BIGENTRY_TIM8;
		else if (BigFileScanTIM(f, entry, TIM_16Bit))
			entry->type = BIGENTRY_TIM16;
		else if (BigFileScanTIM(f, entry, TIM_24Bit))
			entry->type = BIGENTRY_TIM24;
		else if (BigFileScanRiffWave(f, entry))
			entry->type = BIGENTRY_RIFF_WAVE;
		else
			entry->type = BIGENTRY_UNKNOWN;
		stats[entry->type]++;
		sprintf(entry->name, "%.8X.%s", entry->hash, bigentryext[entry->type]);
	}
	fsetpos(f, &fpos);
	printf("\n");
	// print stats
	printf(" %6i 4-bit TIM\n", stats[BIGENTRY_TIM4]);
	printf(" %6i 8-bit TIM\n", stats[BIGENTRY_TIM8]);
	printf(" %6i 16-bit TIM\n", stats[BIGENTRY_TIM16]);
	printf(" %6i 24-bit TIM\n", stats[BIGENTRY_TIM24]);
	printf(" %6i RAW VAG\n", stats[BIGENTRY_RAW_VAG]);
	printf(" %6i RIFF WAVE\n", stats[BIGENTRY_RIFF_WAVE]);
	printf(" %6i unknown\n", stats[BIGENTRY_UNKNOWN]);
}


/*
==========================================================================================

  BigFile analyser

==========================================================================================
*/

typedef struct
{
	unsigned int data;
	int occurrences;
}
bigchunk4_t;

typedef struct
{
	unsigned int data;
	int occurrences;
}
bigchunk8_t;

typedef struct
{
	// unsigned int chunks
	bigchunk4_t chunks4[2048];
	byte chunk4;
	int numchunks4;
}
bigchunkstats_t;

int BigFile_Analyse(int argc, char **argv, char *outfile)
{
	FILE *f;
	bigfileheader_t *data;
	bigfileentry_t *entry;
	bigchunkstats_t *chunkstats;
	int i, k;

	// open file & load header
	f = SafeOpen(bigfile, "rb");
	data = ReadBigfileHeader(f, bigfile, false);
	BigfileScanFiletypes(f, data);

	// analyse headers
	chunkstats = (bigchunkstats_t *)qmalloc(sizeof(bigchunkstats_t));
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		if (entry->type != BIGENTRY_UNKNOWN)
			continue;
		printf("\ranalysing entry %i of %i", i + 1, data->numentries);
		fflush(stdout);

		// chunk4 stats
		BigfileSeekFile(f, entry);
		if (fread(&chunkstats->chunk4, sizeof(unsigned int), 1, f) > 0)
		{
			// try find chunk
			for (k = 0; k < chunkstats->numchunks4; k++)
			{
				if (chunkstats->chunks4[k].data != chunkstats->chunk4)
					continue;
				chunkstats->chunks4[k].occurrences++;
				break;
			}
			if (k >= chunkstats->numchunks4) // not found, allocate new
			{
				chunkstats->chunks4[chunkstats->numchunks4].data = chunkstats->chunk4;
				chunkstats->chunks4[chunkstats->numchunks4].occurrences = 1;
				chunkstats->numchunks4++;
			}
		}
	}
	printf("\n");

	// print stats
	printf("=== Chunk (unsigned int) ===\n");
	printf("  occurence threshold = 4\n");
	for (i = 0; i < chunkstats->numchunks4; i++)
		if (chunkstats->chunks4[i].occurrences > 4)
			printf("  %.8X = %i occurences\n", chunkstats->chunks4[i].data, chunkstats->chunks4[i].occurrences);

	fclose(f);
	qfree(chunkstats);

	return 0;

}

/*
==========================================================================================

  Actions

==========================================================================================
*/

int BigFile_List(int argc, char **argv, char *listfile)
{
	FILE *f, *f2;
	bigfileheader_t *data;

	// open file & load header
	f = SafeOpen(bigfile, "rb");
	data = ReadBigfileHeader(f, bigfile, false);
	BigfileScanFiletypes(f, data);

	// print or...
	if (listfile[0] == '-')
		BigfileWriteListfile(stdout, data);
	else // output to file
	{
		f2 = SafeOpen(listfile, "w");
		BigfileWriteListfile(f2, data);
		printf("wrote %s\n", listfile);
		fclose(f2);
	}
	printf("done.\n");

	fclose (f);

	return 0;
}


int BigFile_Unpack(int argc, char **argv, char *dstdir, qboolean tim2tga, qboolean bpp16to24)
{
	FILE *f, *f2;
	char savefile[MAX_BLOODPATH];
	bigfileheader_t *data;
	bigfileentry_t *entry;
	tim_image_t *tim;
	int i;

	// open file & load header
	f = SafeOpen(bigfile, "rb");
	data = ReadBigfileHeader(f, bigfile, false);
	BigfileScanFiletypes(f, data);

	// make directory
	printf("%s folder created\n", dstdir);
	Q_mkdir(dstdir);

	if (tim2tga)
		printf("TIM->TGA conversion enabled\n");
	if (bpp16to24)
		printf("Targa compatibility mode enabled (converting 16-bit to 24-bit)\n");

	// export all files
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		// original pill.big has 'funky' files with zero len, export them as empty ones
		if (entry->size <= 0) 
		{
			sprintf(savefile, "%s/%.8X.%s", dstdir, entry->hash, bigentryext[entry->type]);
			f2 = SafeOpen(savefile, "wb");
			fclose(f2);
			continue;
		}

		printf("\runpacking file %i of %i", i + 1, data->numentries);
		fflush(stdout);

		// extract TGA
		if ((entry->type == BIGENTRY_TIM8 || entry->type == BIGENTRY_TIM16 || entry->type == BIGENTRY_TIM24) && tim2tga)
		{
			BigfileSeekFile(f, entry);
			tim = TIM_LoadFromStream(f, (int)entry->size);
			if (!tim->error)
			{
				sprintf(savefile, "%s/%.8X.tga", dstdir, entry->hash);
				sprintf(entry->name, "%.8X.tga", entry->hash); // write 'good' listfile.txt
				TIM_WriteTarga(tim, savefile, bpp16to24);
				FreeTIM(tim);
				continue;
			}
			FreeTIM(tim);
		}

		// extract original file
		sprintf(savefile, "%s/%.8X.%s", dstdir, entry->hash, bigentryext[entry->type]);
		f2 = SafeOpen(savefile, "wb");
		BigFileUnpackFile(f, entry, f2);
		fclose(f2);
	}
	printf("\n");

	// write listfile
	sprintf(savefile, "%s/listfile.txt", dstdir);
	f2 = SafeOpen(savefile, "w");
	BigfileWriteListfile(f2, data);
	fclose(f2);
	printf("wrote %s\n", savefile);
	printf("done.\n");

	fclose (f);
	return 0;
}

int BigFile_Pack(int argc, char **argv, char *srcdir)
{
	FILE *f;
	bigfileheader_t *data;
	bigfileentry_t *entry;
	char savefile[MAX_BLOODPATH];
	byte *contents;
	int i, size;

	data = BigfileOpenListfile(srcdir);

	// open bigfile
	f = fopen(bigfile, "rb");
	if (f)
	{
		printf("%s already exists, overwriting\n", bigfile);
		fclose(f);
	}
	f = SafeOpen(bigfile, "wb");

	// write header
	fwrite(&data->numentries, 4, 1, f);
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		printf("\rwriting header %i of %i", i + 1, data->numentries);
		fflush(stdout);
	
		fwrite(&entry->hash, 4, 1, f);
		fwrite(&entry->size, 4, 1, f);
		fwrite(&entry->offset, 4, 1, f);
	}
	printf("\n");

	// write files
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		printf("\rwriting entry %i of %i", i + 1, data->numentries);
		fflush(stdout);

		// if file is already loaded
		if (entry->data != NULL)
		{
			fwrite(entry->data, entry->size, 1, f);
			continue;
		}
		// read file from HDD
		sprintf(savefile, "%s/%s", srcdir, entry->name);
		size = LoadFile(savefile, &contents);
		if (size != (int)entry->size)
			Error("entry %.8X: file size changed while packing\n", entry->hash);
		fwrite(contents, size, 1, f);
		qfree(contents);
	}
	printf("\n");

	fclose (f);
	printf("done.\n");

	return 0;
}

/*
==========================================================================================

  Main

==========================================================================================
*/

int BigFile_Main(int argc, char **argv)
{
	int i = 1, k, returncode = 0;
	char *tofile, *srcdir, *dstdir, *knownfiles, *c;
	qboolean tim2tga, bpp16to24;

	printf("=== BigFile ===\n");
	if (i < 1)
		Error("not enough parms");

	// get input file
	c = argv[i];
	if (c[0] != '-')
	{
		strcpy(bigfile, c);
		i++;
	}
	else
		strcpy(bigfile, "pill.big");

	// args check
	if (argc < i + 1)
		Error("no action specified, try %s -help", progname);

	// parse cmdline
	tofile = qmalloc(MAX_BLOODPATH);
	srcdir = qmalloc(MAX_BLOODPATH);
	dstdir = qmalloc(MAX_BLOODPATH);
	knownfiles = qmalloc(MAX_BLOODPATH);
	strcpy(tofile, "-");
	strcpy(dstdir, DEFAULT_PACKPATH);
	strcpy(srcdir, DEFAULT_PACKPATH);
	strcpy(knownfiles, "-");
	tim2tga = false;
	bpp16to24 = false;
	for (k = 2; k < argc; k++)
	{
		if (!strcmp(argv[k],"-to"))
		{
			k++;
			if (k < argc)
				strcpy(tofile, argv[k]);
		}
		else if (!strcmp(argv[k],"-dstdir"))
		{
			k++;
			if (k < argc)
				strcpy(dstdir, argv[k]);
		}
		else if (!strcmp(argv[k],"-srcdir"))
		{
			k++;
			if (k < argc)
				strcpy(srcdir, argv[k]);
		}
		else if (!strcmp(argv[k],"-klist"))
		{
			k++;
			if (k < argc)
				strcpy(knownfiles, argv[k]);
		}
		else if (!strcmp(argv[k],"-tim2tga"))
			tim2tga = true;
		else if (!strcmp(argv[k],"-16to24"))
			bpp16to24 = true;
	}
	
	// load up knowledge base
	if (knownfiles[0] == '-')
		bigklist = BigfileEmptyKList();
	else
		bigklist = BigfileLoadKList(knownfiles);

	// action
	if (!strcmp(argv[i], "-list"))
		returncode = BigFile_List(argc-i, argv+i, tofile);
	else if (!strcmp(argv[i], "-analyse"))
		returncode = BigFile_Analyse(argc-i, argv+i, tofile);
	else if (!strcmp(argv[i], "-unpack"))
		returncode = BigFile_Unpack(argc-i, argv+i, dstdir, tim2tga, bpp16to24);
	else if (!strcmp(argv[i], "-pack"))
		returncode = BigFile_Pack(argc-i, argv+i, srcdir);
	else
		printf("unknown option %s", argv[i]);

	return returncode;
}