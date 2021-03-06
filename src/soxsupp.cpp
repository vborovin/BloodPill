// SoX support
// pretty hacky yet
// need a proper soxlib support

#include "bloodpill.h"
#include "soxsupp.h"

#if defined(WIN32) || defined(_WIN64)
#include "windows.h"
#include <shellapi.h> 
#pragma comment(lib, "shell32.lib") 
#endif

bool soxfound = false;
char soxpath[MAX_OSPATH];

/*
==========================================================================================

  General SoX support

==========================================================================================
*/

bool SoX_Init(char *pathtoexe)
{
#if defined(WIN32) || defined(_WIN64)
	soxfound = false;
	sprintf(soxpath, "%s%s", progpath, pathtoexe);
	if (!FileExists(soxpath))
	{
		// try search sox/
		sprintf(soxpath, "%ssox\\%s", progpath, pathtoexe);
		if (!FileExists(soxpath))
			soxfound = false;
		else
			soxfound = true;
	}
	else
		soxfound = true;
#else
	soxfound = false;
#endif
	return soxfound;
}

// runs SoX on files presented, returs TRUE if succesful
bool SoX(char *in, char *generalcmd, char *inputcmd, char *outputcmd, char *out, char *effects)
{
#if defined(WIN32) || defined(_WIN64)
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	DWORD exitcode = 0; 
	char cmd[2048];

	if (!soxfound)
	{
	//	SetLastError(SOXSUPP_ERROR_SOXNOTFOUND);
		return false;
	}

	// run progress
	GetStartupInfo(&si);
	if (noprint)
		sprintf(cmd, "%s -V0 %s %s \"%s\" %s \"%s\" %s", soxpath, generalcmd, inputcmd, in, outputcmd, out, effects);
	else
		sprintf(cmd, "%s %s %s \"%s\" %s \"%s\" %s", soxpath, generalcmd, inputcmd, in, outputcmd, out, effects);
	//printf("\n\nSoX: %s\n", cmd);
	memset(&pi, 0, sizeof(PROCESS_INFORMATION)); 
	if (!CreateProcess(NULL, cmd, NULL, NULL, false, 0, NULL, NULL, &si, &pi))
		return false;
	exitcode = WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
#else
//	SetLastError(SOXSUPP_ERROR_PROCESSFAIL);
	return false;
#endif
}

// runs SoX on data presented and allocates output data
bool SoX_DataToData(byte *data, int databytes, char *generalcmd, char *inputcmd, char *outputcmd, byte **outdataptr, int *outdatabytesptr, char *effects)
{	
	char in[MAX_OSPATH], out[MAX_OSPATH];
	byte *outdata;
	int outdatabytes;
	bool sox;
	FILE *f;

	TempFileName(out);
	TempFileName(in);

	// make input
	f = SafeOpen(in, "wb");
	fwrite(data, databytes, 1, f);
	fclose(f);

	// run
	sox = SoX(in, generalcmd, inputcmd, outputcmd, out, effects);
	if (!sox)
		return false;

	// read contents of out file
	outdatabytes = LoadFile(out, &outdata);
	*outdataptr = outdata;
	*outdatabytesptr = outdatabytes;

	// remove tempfiles
	remove(in);
	remove(out);
	return true;
}

// runs SoX on file and allocates output data
bool SoX_FileToData(char *in, char *generalcmd, char *inputcmd, char *outputcmd, int *outdatabytesptr, byte **outdataptr, char *effects)
{	
	char out[MAX_OSPATH];
	byte *outdata;
	int outdatabytes;
	bool sox;
	FILE *f;

	TempFileName(out);

	// run
	sox = SoX(in, generalcmd, inputcmd, outputcmd, out, effects);
	if (!sox)
		return false;

	// read contents of out file
	f = SafeOpen(out, "rb");
	outdatabytes = Q_filelength(f);
	outdata = (byte *)mem_alloc(outdatabytes);
	fread(outdata, outdatabytes, 1, f);
	*outdataptr = outdata;
	*outdatabytesptr = outdatabytes;
	fclose(f);

	remove(out);

	return true;
}

/*
==========================================================================================

  Vag2Wav converter

==========================================================================================
*/

int AdpcmConvert_Main(int argc, char **argv)
{
	char filename[MAX_OSPATH], outfile[MAX_OSPATH], basefilename[MAX_OSPATH], ext[15];
	char inputcmd[2048], outputcmd[2048], *c;
	int rate, i = 1;
	double vorbisquality;
	bool wavpcm;

	Print("=== AdpcmConvert ==\n");
	if (i < 1)
		Error("not enough parms");

	// get inner file
	strcpy(filename, argv[i]);
	StripFileExtension(filename, basefilename);
	ExtractFileExtension(filename, ext);
	Q_strlower(ext);
	i++;

	// get out file
	sprintf(outfile, "%s.wav", basefilename); 
	if (i < argc)
	{
		c = argv[i];
		if (c[0] != '-')
			strcpy(outfile, c);
	}

	// parse cmdline
	sprintf(outputcmd, "-t wav");
	wavpcm = false;
	rate = 22050;
	for (; i < argc; i++)
	{
		if (!strcmp(argv[i], "-pcm"))
		{
			wavpcm = true;
			Verbose("Option: export 16-bit PCM RIFF WAVE\n");
			sprintf(outputcmd, "-t wav -e signed-integer");
		}
		else if (!strcmp(argv[i], "-oggvorbis"))
		{
			i++;
			vorbisquality = 5.0f;
			if (i < argc)
				vorbisquality = atof(argv[i]);
			if (vorbisquality > 10.0f)
				vorbisquality = 10.0f;
			if (vorbisquality < 0.0f)
				vorbisquality = 0.0f;
			Verbose("Option: export Ogg Vorbis (quality %f)\n", vorbisquality);
			sprintf(outputcmd, "-t ogg -C %f", vorbisquality);
		}
		else if (!strcmp(argv[i], "-custom"))
		{
			i++;
			if (i < argc)
				sprintf(outputcmd, "%s", argv[i]);
			Verbose("Option: custom SoX commandline (%s)\n", outputcmd);
		}
		else if (!strcmp(argv[i], "-rate"))
		{
			i++;
			if (i < argc)
				rate = atoi(argv[i]);
			Verbose("Option: rate %ihz\n", rate);
		}
	}

	// build outputcmd
	if (!strcmp(ext, "vag"))
		sprintf(inputcmd, "-t ima -r %i -c 1", rate);
	else
	{
		Warning("file %s is not a APDCM file, detecting type automatically\n", outputcmd);
		sprintf(inputcmd, "");
	}

	// run SOX
	Print("conversion in progress...\n");
	if (!SoX(filename, "", inputcmd, outputcmd, outfile, ""))
		//Error("SoX Error: #%i", GetLastError());
		Error("SoX Error");
	Print("done.\n");
	return 0;
}
