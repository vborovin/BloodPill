// filetypes
#define BIGFILE_NUM_FILETYPES 7
typedef enum
{
	BIGENTRY_UNKNOWN,		// unknown data
	BIGENTRY_TIM4,			// 4 bit TIM texture
	BIGENTRY_TIM8,			// 8 bit TIM texture
	BIGENTRY_TIM16,			// 16 bit TIM texture
	BIGENTRY_TIM24,			// 24 bit TIM texture
	BIGENTRY_RAW_VAG,		// RAW 4 bit ADPCM
	BIGENTRY_RIFF_WAVE,		// RIFF wave file
}bigentrytype_t;

// filetype extensions
static char *bigentryext[BIGFILE_NUM_FILETYPES] = 
{ 
	"dat", 
	"tim",
	"tim",
	"tim",
	"tim",
	"vag", 
	"wav"
};

// bigfile entry
typedef struct
{
	// base info
	unsigned int hash; // hashed name
	unsigned int size; // file size
	unsigned int offset; // file offset
	// used by tool
	bigentrytype_t type;
	char name[16];
	int samplingrate; // for VAG 
	byte *data; // only presented if loaded
}
bigfileentry_t;

// bigfile header
typedef struct
{
		bigfileentry_t *entries;
		unsigned int	numentries;
}
bigfileheader_t;