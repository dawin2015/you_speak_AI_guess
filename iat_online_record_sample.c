/*
* 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。
*/

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <errno.h>
#include <process.h>

//#include "msp_cmn.h"
//#include "msp_errors.h"
#include "c:/cppProjects/Windows_awaken_exp1218_iat1218_tts_online1218_5c1e0057/include/msp_cmn.h"
#include "c:/cppProjects/Windows_awaken_exp1218_iat1218_tts_online1218_5c1e0057/include/msp_errors.h"
#include "./include/speech_recognizer.h"

#include "c:/cppProjects/Windows_awaken_exp1218_iat1218_tts_online1218_5c1e0057/include/qivw.h"

#include "qtts.h"
#include <windows.h>
#include <MMSYSTEM.H>
#include <playsoundapi.h>

#ifdef _WIN64
#pragma comment(lib,"../../libs/msc_x64.lib")
#else
#pragma comment(lib, "../../libs/msc.lib")
#endif

#define FRAME_LEN	640 
#define	BUFFER_SIZE	4096

#define IVW_AUDIO_FILE_NAME "audio/awake.pcm"
CRITICAL_SECTION cs;

enum{
	EVT_START = 0,
	EVT_STOP,
	EVT_QUIT,
	EVT_TOTAL
};
//static HANDLE events[EVT_TOTAL] = {NULL,NULL,NULL};
static HANDLE events[EVT_TOTAL] = { NULL,NULL};

static COORD begin_pos = {0, 0};
static COORD last_pos = {0, 0};

static void show_result(char *string, char is_over)
{
	COORD orig, current;
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE w = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(w, &info);
	current = info.dwCursorPosition;

	if(current.X == last_pos.X && current.Y == last_pos.Y ) {
		SetConsoleCursorPosition(w, begin_pos);
	} else {
		/* changed by other routines, use the new pos as start */
		begin_pos = current;
	}
	if(is_over)
		SetConsoleTextAttribute(w, FOREGROUND_GREEN);
	// TODO
	printf("Result: [ %s ]\n", string);
	if(is_over)
		SetConsoleTextAttribute(w, info.wAttributes);

	GetConsoleScreenBufferInfo(w, &info);
	last_pos = info.dwCursorPosition;
}

static void show_key_hints(void)
{
	printf("\n\
----------------------------\n\
Press r to start speaking\n\
Press s to end your speaking\n\
Press q to quit\n\
----------------------------\n");
}

/* 上传用户词表 */
static int upload_userwords()
{
	char*			userwords	=	NULL;
	size_t			len			=	0;
	size_t			read_len	=	0;
	FILE*			fp			=	NULL;
	int				ret			=	-1;

	fp = fopen("userwords.txt", "rb");
	if (NULL == fp)										
	{
		printf("\nopen [userwords.txt] failed! \n");
		goto upload_exit;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp); //获取文件大小
	fseek(fp, 0, SEEK_SET);  					
	
	userwords = (char*)malloc(len + 1);
	if (NULL == userwords)
	{
		printf("\nout of memory! \n");
		goto upload_exit;
	}

	read_len = fread((void*)userwords, 1, len, fp); //读取用户词表内容
	if (read_len != len)
	{
		printf("\nread [userwords.txt] failed!\n");
		goto upload_exit;
	}
	userwords[len] = '\0';
	
	MSPUploadData("userwords", userwords, len, "sub = uup, dtt = userword", &ret); //上传用户词表
	if (MSP_SUCCESS != ret)
	{
		printf("\nMSPUploadData failed ! errorCode: %d \n", ret);
		goto upload_exit;
	}
	
upload_exit:
	if (NULL != fp)
	{
		fclose(fp);
		fp = NULL;
	}	
	if (NULL != userwords)
	{
		free(userwords);
		userwords = NULL;
	}
	
	return ret;
}

/* helper thread: to listen to the keystroke */
static unsigned int  __stdcall helper_thread_proc ( void * para)
{
	int key;
	int quit = 0;

	do {
		key = _getch();
		switch(key) {
		case 'r':
		case 'R':
			SetEvent(events[EVT_START]);
			break;
		case 's':
		case 'S':
			SetEvent(events[EVT_STOP]);
			break;
		case 'q':
		case 'Q':
			quit = 1;
			SetEvent(events[EVT_QUIT]);
			PostQuitMessage(0);
			break;
		default:
			break;
		}

		if(quit)
			break;		
	} while (1);

	return 0;
}

static HANDLE start_helper_thread()
{
	HANDLE hdl;

	hdl = (HANDLE)_beginthreadex(NULL, 0, helper_thread_proc, NULL, 0, NULL);

	return hdl;
}

static char *g_result = NULL;
static unsigned int g_buffersize = BUFFER_SIZE;

void on_result(const char *result, char is_last)
{
	if (result) {
		size_t left = g_buffersize - 1 - strlen(g_result);
		size_t size = strlen(result);
		if (left < size) {
			g_result = (char*)realloc(g_result, g_buffersize + BUFFER_SIZE);
			if (g_result)
				g_buffersize += BUFFER_SIZE;
			else {
				printf("mem alloc failed\n");
				return;
			}
		}
		strncat(g_result, result, size);
		show_result(g_result, is_last);
	}
}

/* wav音频头部格式 */
typedef struct _wave_pcm_hdr
{
	char            riff[4];                // = "RIFF"
	int				size_8;                 // = FileSize - 8
	char            wave[4];                // = "WAVE"
	char            fmt[4];                 // = "fmt "
	int				fmt_size;				// = 下一个结构体的大小 : 16

	short int       format_tag;             // = PCM : 1
	short int       channels;               // = 通道数 : 1
	int				samples_per_sec;        // = 采样率 : 8000 | 6000 | 11025 | 16000
	int				avg_bytes_per_sec;      // = 每秒字节数 : samples_per_sec * bits_per_sample / 8
	short int       block_align;            // = 每采样点字节数 : wBitsPerSample / 8
	short int       bits_per_sample;        // = 量化比特数: 8 | 16

	char            data[4];                // = "data";
	int				data_size;              // = 纯数据长度 : FileSize - 44 
} wave_pcm_hdr;

/* 默认wav音频头部数据 */
wave_pcm_hdr default_wav_hdr =
{
	{ 'R', 'I', 'F', 'F' },
	0,
	{ 'W', 'A', 'V', 'E' },
	{ 'f', 'm', 't', ' ' },
	16,
	1,
	1,
	16000,
	32000,
	2,
	16,
	{ 'd', 'a', 't', 'a' },
	0
};
/* 文本合成 */
int text_to_speech(const char* src_text, const char* des_path, const char* params)
{
	int          ret = -1;
	FILE*        fp = NULL;
	const char*  sessionID = NULL;
	unsigned int audio_len = 0;
	wave_pcm_hdr wav_hdr = default_wav_hdr;
	int          synth_status = MSP_TTS_FLAG_STILL_HAVE_DATA;

	if (NULL == src_text || NULL == des_path)
	{
		printf("params is error!\n");
		return ret;
	}
	fp = fopen(des_path, "wb");
	if (NULL == fp)
	{
		printf("open %s error.\n", des_path);
		return ret;
	}
	/* 开始合成 */
	sessionID = QTTSSessionBegin(params, &ret);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionBegin failed, error code: %d.\n", ret);
		fclose(fp);
		return ret;
	}
	ret = QTTSTextPut(sessionID, src_text, (unsigned int)strlen(src_text), NULL);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSTextPut failed, error code: %d.\n", ret);
		QTTSSessionEnd(sessionID, "TextPutError");
		fclose(fp);
		return ret;
	}
	printf("正在合成 ...\n");
	fwrite(&wav_hdr, sizeof(wav_hdr), 1, fp); //添加wav音频头，使用采样率为16000
	while (1)
	{
		/* 获取合成音频 */
		const void* data = QTTSAudioGet(sessionID, &audio_len, &synth_status, &ret);
		if (MSP_SUCCESS != ret)
			break;
		if (NULL != data)
		{
			fwrite(data, audio_len, 1, fp);
			wav_hdr.data_size += audio_len; //计算data_size大小
		}
		if (MSP_TTS_FLAG_DATA_END == synth_status)
			break;
		printf(">");
		Sleep(150); //防止频繁占用CPU
	}
	printf("\n");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSAudioGet failed, error code: %d.\n", ret);
		QTTSSessionEnd(sessionID, "AudioGetError");
		fclose(fp);
		return ret;
	}
	/* 修正wav文件头数据的大小 */
	wav_hdr.size_8 += wav_hdr.data_size + (sizeof(wav_hdr) - 8);

	/* 将修正过的数据写回文件头部,音频文件为wav格式 */
	fseek(fp, 4, 0);
	fwrite(&wav_hdr.size_8, sizeof(wav_hdr.size_8), 1, fp); //写入size_8的值
	fseek(fp, 40, 0); //将文件指针偏移到存储data_size值的位置
	fwrite(&wav_hdr.data_size, sizeof(wav_hdr.data_size), 1, fp); //写入data_size的值
	fclose(fp);
	fp = NULL;
	/* 合成完毕 */
	ret = QTTSSessionEnd(sessionID, "Normal");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionEnd failed, error code: %d.\n", ret);
	}

	return ret;
}

void on_speech_begin()
{
	if (g_result)
	{
		free(g_result);
	}
	g_result = (char*)malloc(BUFFER_SIZE);
	g_buffersize = BUFFER_SIZE;
	memset(g_result, 0, g_buffersize);

	int         tts_ret = MSP_SUCCESS;
	const char* tts_session_begin_params = "voice_name = xiaoyan, text_encoding = gb2312, sample_rate = 16000, speed = 50, volume = 50, pitch = 50, rdn = 2";
	const char* filename = "tts_sample.wav"; //合成的语音文件名称
	const char* tts_text = "新年好，我在听，请说"; //合成文本
	tts_ret = text_to_speech(tts_text, filename, tts_session_begin_params);
	if (MSP_SUCCESS != tts_ret)
	{
		printf("text_to_speech failed, error code: %d.\n", tts_ret);
	}
	PlaySound(TEXT("C:\\cppProjects\\Windows_awaken_exp1218_iat1218_tts_online1218_5c1e0057\\bin\\tts_sample.wav"), NULL, SND_FILENAME | SND_SYNC);
	printf("Start Listening...\n");
}
void on_speech_end(int reason)
{
	if (reason == END_REASON_VAD_DETECT)
		printf("\nSpeaking done \n");
	else
		printf("\nRecognizer error %d\n", reason);
}

void run_ivw(char *databuf, long data_size);
void stop_ivw();
int start_ivw(const char *grammar_list, const char* audio_filename, const char* session_begin_params);
void forRec(void *ptr);
void init_mic();

/* demo send audio data from a file */
static void demo_file(const char* audio_file, const char* session_begin_params)
{
	unsigned int	total_len = 0;
	int				errcode = 0;
	FILE*			f_pcm = NULL;
	char*			p_pcm = NULL;
	unsigned long	pcm_count = 0;
	unsigned long	pcm_size = 0;
	unsigned long	read_size = 0;
	struct speech_rec iat;
	struct speech_rec_notifier recnotifier = {
		on_result,
		on_speech_begin,
		on_speech_end
	};

	if (NULL == audio_file)
		goto iat_exit;

	f_pcm = fopen(audio_file, "rb");
	if (NULL == f_pcm)
	{
		printf("\nopen [%s] failed! \n", audio_file);
		goto iat_exit;
	}

	fseek(f_pcm, 0, SEEK_END);
	pcm_size = ftell(f_pcm); //获取音频文件大小 
	fseek(f_pcm, 0, SEEK_SET);

	p_pcm = (char *)malloc(pcm_size);
	if (NULL == p_pcm)
	{
		printf("\nout of memory! \n");
		goto iat_exit;
	}

	read_size = fread((void *)p_pcm, 1, pcm_size, f_pcm); //读取音频文件内容
	if (read_size != pcm_size)
	{
		printf("\nread [%s] error!\n", audio_file);
		goto iat_exit;
	}

	errcode = sr_init(&iat, session_begin_params, SR_USER, 0, &recnotifier);
	if (errcode) {
		printf("speech recognizer init failed : %d\n", errcode);
		goto iat_exit;
	}

	errcode = sr_start_listening(&iat);
	if (errcode) {
		printf("\nsr_start_listening failed! error code:%d\n", errcode);
		goto iat_exit;
	}

	while (1)
	{
		unsigned int len = 10 * FRAME_LEN; // 每次写入200ms音频(16k，16bit)：1帧音频20ms，10帧=200ms。16k采样率的16位音频，一帧的大小为640Byte
		int ret = 0;

		if (pcm_size < 2 * len)
			len = pcm_size;
		if (len <= 0)
			break;

		printf(">");
		ret = sr_write_audio_data(&iat, &p_pcm[pcm_count], len);

		if (0 != ret)
		{
			printf("\nwrite audio data failed! error code:%d\n", ret);
			goto iat_exit;
		}

		pcm_count += (long)len;
		pcm_size -= (long)len;		
	}

	errcode = sr_stop_listening(&iat);
	if (errcode) {
		printf("\nsr_stop_listening failed! error code:%d \n", errcode);
		goto iat_exit;
	}

iat_exit:
	if (NULL != f_pcm)
	{
		fclose(f_pcm);
		f_pcm = NULL;
	}
	if (NULL != p_pcm)
	{
		free(p_pcm);
		p_pcm = NULL;
	}

	sr_stop_listening(&iat);
	sr_uninit(&iat);
}

int g_ivw_result = 0;

/* demo recognize the audio from microphone */
static void demo_mic(const char* session_begin_params)
{
	int errcode;
	int i = 0;
	HANDLE helper_thread = NULL;

	struct speech_rec iat;
	DWORD waitres;
	char isquit = 0;

	struct speech_rec_notifier recnotifier = {
		on_result,
		on_speech_begin,
		on_speech_end
	};

	errcode = sr_init(&iat, session_begin_params, SR_MIC, DEFAULT_INPUT_DEVID, &recnotifier);
	if (errcode) {
		printf("speech recognizer init failed\n");
		return;
	}

	for (i = 0; i < EVT_TOTAL; ++i) {
		events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
	}

	helper_thread = start_helper_thread();
	if (helper_thread == NULL) {
		printf("create thread failed\n");
		goto exit;
	}

	show_key_hints();

	if (g_ivw_result)
	{
		Sleep(10000);
		SetEvent(events[EVT_START]);		
	}

 	while (1) {
		waitres = WaitForMultipleObjects(EVT_TOTAL, events, FALSE, INFINITE);
		switch (waitres) {
		case WAIT_FAILED:
		case WAIT_TIMEOUT:
			printf("Why it happened !?\n");
			break;
		case WAIT_OBJECT_0 + EVT_START:
			if (errcode = sr_start_listening(&iat)) {
				printf("start listen failed %d\n", errcode);
				isquit = 1;
			}
			break;
		case WAIT_OBJECT_0 + EVT_STOP:		
			if (errcode = sr_stop_listening(&iat)) {
				printf("stop listening failed %d\n", errcode);
				isquit = 1;
			}
			break;
		case WAIT_OBJECT_0 + EVT_QUIT:
			sr_stop_listening(&iat);
			isquit = 1;
			break;
		default:
			break;
		}
		if (isquit)
			break;
	}

exit:
	if (helper_thread != NULL) {
		WaitForSingleObject(helper_thread, INFINITE);
		CloseHandle(helper_thread);
	}
	
	for (i = 0; i < EVT_TOTAL; ++i) {
		if (events[i])
			CloseHandle(events[i]);
	}

	sr_uninit(&iat);
}

// dawin 20181228 add
#define BUFFSIZE 1024 * 1024 //环形缓冲区的大小，你可以定义大一些
#define BUFFER_SIZE 4096


//环形缓冲区的的数据结构
struct cycle_buffer {
	char *buf;
	unsigned int size;
	unsigned int in;
	unsigned int out;
};

static struct cycle_buffer *fifo = NULL;//定义全局FIFO
FILE *fp;
CRITICAL_SECTION cs;

//初始化环形缓冲区
static int init_cycle_buffer(void)
{
	int size = BUFFSIZE, ret;

	ret = size & (size - 1);
	if (ret)
		return ret;
	fifo = (struct cycle_buffer *) malloc(sizeof(struct cycle_buffer));
	if (!fifo)
		return -1;

	memset(fifo, 0, sizeof(struct cycle_buffer));
	fifo->size = size;
	fifo->in = fifo->out = 0;
	fifo->buf = (char *)malloc(size);
	if (!fifo->buf)
		free(fifo);
	else
		memset(fifo->buf, 0, size);
	return 0;
}

unsigned int fifo_get(char *buf, unsigned int len)  //从环形缓冲区中取数据
{
	unsigned int l;
	len = min(len, fifo->in - fifo->out);
	l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));
	memcpy(buf, fifo->buf + (fifo->out & (fifo->size - 1)), l);
	memcpy(buf + l, fifo->buf, len - l);
	fifo->out += len;
	return len;
}

unsigned int fifo_put(char *buf, unsigned int len) //将数据放入环形缓冲区
{
	unsigned int l;
	len = min(len, fifo->size - fifo->in + fifo->out);
	l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));
	memcpy(fifo->buf + (fifo->in & (fifo->size - 1)), buf, l);
	memcpy(fifo->buf, buf + l, len - l);
	fifo->in += len;
	return len;
}
// dawin 20181228 end

// dawin 20181228 add
boolean win;//采集数据标志位
FILE *f;//存储采集到的音频文件
HWAVEIN hWaveIn;  //输入设备
WAVEFORMATEX waveform; //采集音频的格式，结构体
BYTE *pBuffer1, *pBuffer2;//采集音频时的数据缓存
WAVEHDR wHdr1, wHdr2; //采集音频时包含数据缓存的结构体



// dawin 20181228 end
FILE *record_f;
//回调函数当数据缓冲区慢的时候就会触发，回调函数，执行下面的RecordWave函数之后相当于创建了一个线程
DWORD CALLBACK MicCallback(HWAVEIN hwavein, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	int len = 0;
	switch (uMsg)
	{
		//打开设备时这个分支会执行。
	case WIM_OPEN:
		printf("\n设备已经打开...\n");
		break;
		//当缓冲区满的时候这个分支会执行，不要再这个分支中出现阻塞语句，会丢数据	，waveform audio本身没有缓冲机制。
	case WIM_DATA:
		printf("\n缓冲区%d存满...\n", ((LPWAVEHDR)dwParam1)->dwUser);
		if (win)
		{

			//进入临界区
			EnterCriticalSection(&cs);
			//将缓冲区的数据写入环形fifo
			len = fifo_put(((LPWAVEHDR)dwParam1)->lpData, ((PWAVEHDR)dwParam1)->dwBytesRecorded);
			//退出临界区
			LeaveCriticalSection(&cs);
			waveInAddBuffer(hwavein, (PWAVEHDR)dwParam1, sizeof(WAVEHDR));
		}
		if (record_f != NULL)
		{
			fwrite(((PWAVEHDR)dwParam1)->lpData, ((PWAVEHDR)dwParam1)->dwBytesRecorded, 1, record_f);
		}
		break;
	case WIM_CLOSE:
		printf("\n设备已经关闭...\n");
		break;
	default:
		break;
	}
	return 0;
}

const char *wakeup_session_id = NULL;
#define SAMPLERATE 16000
#define MAX_UTT_LEN 160000
void init_mic()
{
	waveform.wFormatTag = WAVE_FORMAT_PCM;//声音格式为PCM
	waveform.nSamplesPerSec = SAMPLERATE;//采样率，16000次/秒
	waveform.wBitsPerSample = 16;//采样比特，16bits/次
	waveform.nChannels = 1;//采样声道数，1声道
	waveform.nAvgBytesPerSec = 16000 * 4;//每秒的数据率，就是每秒能采集多少字节的数据
	waveform.nBlockAlign = 2;//一个块的大小，采样bit的字节数乘以声道数
	waveform.cbSize = 0;//一般为0
						//使用waveInOpen函数开启音频采集
	MMRESULT mmr = waveInOpen(&hWaveIn, WAVE_MAPPER, &waveform, (DWORD)(MicCallback), NULL, CALLBACK_FUNCTION);
	if (mmr != MMSYSERR_NOERROR)
		return;
	//建立两个数组（这里可以建立多个数组）用来缓冲音频数据
	DWORD bufsize = 8 * 2048;
	pBuffer1 = malloc(bufsize * sizeof(BYTE));
	pBuffer2 = malloc(bufsize * sizeof(BYTE));
	wHdr1.lpData = (LPSTR)pBuffer1;
	wHdr1.dwBufferLength = bufsize;
	wHdr1.dwBytesRecorded = 0;
	wHdr1.dwUser = 0;
	wHdr1.dwFlags = 0;
	wHdr1.dwLoops = 1;
	wHdr1.lpNext = NULL;
	wHdr1.reserved = 0;
	//将建立好的wHdr1做为备用
	waveInPrepareHeader(hWaveIn, &wHdr1, sizeof(WAVEHDR));
	wHdr2.lpData = (LPSTR)pBuffer2;
	wHdr2.dwBufferLength = bufsize;
	wHdr2.dwBytesRecorded = 0;
	wHdr2.dwUser = 0;
	wHdr2.dwFlags = 0;
	wHdr2.dwLoops = 1;
	wHdr2.lpNext = NULL;
	wHdr2.reserved = 0;
	//将建立好的wHdr2做为备用
	waveInPrepareHeader(hWaveIn, &wHdr2, sizeof(WAVEHDR));
	//将两个wHdr添加到waveIn中去
	waveInAddBuffer(hWaveIn, &wHdr1, sizeof(WAVEHDR));
	waveInAddBuffer(hWaveIn, &wHdr2, sizeof(WAVEHDR));
	//开始音频采集
	waveInStart(hWaveIn);
	win = TRUE;
}

int cb_ivw_msg_proc(const char *sessionID, int msg, int param1, int param2, const void *info, void *userData)
{
	if (MSP_IVW_MSG_ERROR == msg) //唤醒出错消息
	{
		printf("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
	}
	else if (MSP_IVW_MSG_WAKEUP == msg) //唤醒成功消息
	{
		printf("\n\nMSP_IVW_MSG_WAKEUP result = %s\n\n", info);
		// dawin 20181228 add
		win = FALSE;
		//sleep_ms(2000);
		Sleep(2000);
		stop_ivw();
		//init_mic(); // 稍后放在启动语音识别
		//dawin 20181228 end
	}
	return 0;
}

int start_ivw(const char *grammar_list, const char* audio_filename, const char* session_begin_params)
{
	int err_code = MSP_SUCCESS;
	FILE *f_aud = NULL;
	long audio_size = 0;
	long real_read = 0;
	long audio_count = 0;
	int count = 0;
	int audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
	char *audio_buffer = NULL;
	char sse_hints[128];

	fopen_s(&record_f, "录音测试.pcm", "ab");
	wakeup_session_id = QIVWSessionBegin(grammar_list, session_begin_params, &err_code);
	if (err_code != MSP_SUCCESS)
	{
		printf("QIVWSessionBegin failed! error code:%d\n", err_code);
		stop_ivw();
	}

	err_code = QIVWRegisterNotify(wakeup_session_id, cb_ivw_msg_proc, NULL);
	if (err_code != MSP_SUCCESS)
	{
		_snprintf(sse_hints, sizeof(sse_hints), "QIVWRegisterNotify errorCode=%d", err_code);
		printf("QIVWRegisterNotify failed! error code:%d\n", err_code);
		stop_ivw();
	}
	init_mic();
	_beginthread(forRec, 0, NULL);
	return 1;
}

void stop_ivw()
{
	win = FALSE;
	waveInClose(hWaveIn);
}

void run_ivw(char *databuf, long data_size)
{
	if (wakeup_session_id == NULL)
	{
		return;
	}
	int err_code = MSP_SUCCESS;
	long real_read = 0;
	long audio_count = 0;
	int count = 0;
	int audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
	char *audio_buffer = NULL;
	char sse_hints[128];
	long audio_size = data_size;
	char buf[1000000] = "";
	memcpy(buf, databuf, data_size);

	while (audio_size > 0)
	{
		if (win == FALSE)
		{
			printf("leave the ivw run \n");
			break;
		}
		long len = 10 * FRAME_LEN;
		audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if (audio_size <= len)
		{
			len = audio_size;
		}
		printf("csid=%s,count=%d,aus=%d\n", wakeup_session_id, count++, audio_stat);
		err_code = QIVWAudioWrite(wakeup_session_id, (const void *)&buf[audio_count], len, audio_stat);
		if (MSP_SUCCESS != err_code)
		{
			printf("QIVWAudioWrite failed! error code:%d\n", err_code);
			_snprintf(sse_hints, sizeof(sse_hints), "QIVWAudioWrite errorCode=%d", err_code);
			stop_ivw();
		}
		audio_count += len;
		audio_size -= len;
	}
	_snprintf(sse_hints, sizeof(sse_hints), "success");
}

void forRec(void *ptr)
{
	char buff[10240] = { 0 };
	int len = 0;
	while (1)
	{
		EnterCriticalSection(&cs);
		len = fifo_get(buff, 10240);
		LeaveCriticalSection(&cs);

		if (len > 0)
		{
			run_ivw(buff, len);
		}
	}
	printf("leave the forRec thread\n");
}

/* main thread: start/stop record ; query the result of recgonization.
 * record thread: record callback(data write)
 * helper thread: ui(keystroke detection)
 */
int main(int argc, char* argv[])
{
	int			ret						=	MSP_SUCCESS;
	int			upload_on				=	1; //是否上传用户词表
	const char* login_params			=	"appid = 5c1e0057, work_dir = ."; // 登录参数，appid与msc库绑定,请勿随意改动
	int aud_src = 0;

	//const char *ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";

	/*
	* sub:				请求业务类型
	* domain:			领域
	* language:			语言
	* accent:			方言
	* sample_rate:		音频采样率
	* result_type:		识别结果格式
	* result_encoding:	结果编码格式
	*
	*/
	const char* session_begin_params	=	"sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = UTF-8, vad_eos = 10000";

	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, login_params); //第一个参数是用户名，第二个参数是密码，均传NULL即可，第三个参数是登录参数	
	if (MSP_SUCCESS != ret)	{
		printf("MSPLogin failed , Error code %d.\n",ret);
		goto exit; //登录失败，退出登录
	}

	printf("\n########################################################################\n");
	printf("## 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。##\n");
	printf("########################################################################\n\n");
//	printf("演示示例选择:是否上传用户词表？\n0:不使用\n1:使用\n");

//	scanf("%d", &upload_on);
//	if (upload_on)
//	{
//		printf("上传用户词表 ...\n");
//		ret = upload_userwords();
//		if (MSP_SUCCESS != ret)
//			goto exit;	
//		printf("上传用户词表成功\n");
//	}
	const char *ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";
	InitializeCriticalSection(&cs);
	init_cycle_buffer();
	g_ivw_result = start_ivw(NULL, IVW_AUDIO_FILE_NAME, ssb_param);

	demo_mic(session_begin_params);

	//printf("音频数据在哪? \n0: 从文件读入\n1:从MIC说话\n");
	//scanf("%d", &aud_src);
	//if(aud_src != 0) {
	//	demo_mic(session_begin_params);
	//} else {
	//	//iflytek02音频内容为“中美数控”；如果上传了用户词表，识别结果为：“中美速控”。;
	//	demo_file("wav/iflytek02.wav", session_begin_params); 
	//}
exit:
	printf("按任意键退出 ...\n");
	_getch();
	MSPLogout(); //退出登录

	return 0;
}
