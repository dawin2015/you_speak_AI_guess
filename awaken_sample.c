#include "stdlib.h"
#include "stdio.h"
#include <windows.h>
#include <conio.h>
#include <errno.h>

#include "../../include/msp_cmn.h"
#include "../../include/qivw.h"
#include "../../include/msp_errors.h"

// dawin 20181228 add
#include <mmsystem.h>
#include <mmreg.h>
#pragma comment(lib, "winmm.lib")
// dawin 20181228 end

 #ifdef _WIN64
 #pragma comment(lib,"../../libs/msc_x64.lib")
 #else
 #pragma comment(lib, "../../libs/msc.lib")
 #endif

#define IVW_AUDIO_FILE_NAME "audio/awake.pcm"
#define FRAME_LEN	640 //16k采样率的16bit音频，一帧的大小为640B, 时长20ms

// dawin 20181228 add
boolean win;//采集数据标志位
FILE *f;//存储采集到的音频文件
HWAVEIN hWaveIn;  //输入设备
WAVEFORMATEX waveform; //采集音频的格式，结构体
BYTE *pBuffer1, *pBuffer2;//采集音频时的数据缓存
WAVEHDR wHdr1, wHdr2; //采集音频时包含数据缓存的结构体

void run_ivw(char *databuf, long data_size);
void stop_ivw();
void start_ivw(const char *grammar_list, const char *audio_filename, const char *session_begin_params);
void forRec(void *ptr);
void init_mic();

// dawin 20181228 end

void sleep_ms(int ms)
{
	Sleep(ms);
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

int cb_ivw_msg_proc( const char *sessionID, int msg, int param1, int param2, const void *info, void *userData )
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
		init_mic(); // 稍后放在启动语音识别
		//dawin 20181228 end
	}
	return 0;
}
#if 0
void file_run_ivw(const char *grammar_list, const char* audio_filename ,  const char* session_begin_params)
{
	const char *session_id = NULL;
	int err_code = MSP_SUCCESS;
	FILE *f_aud = NULL;
	long audio_size = 0;
	long real_read = 0;
	long audio_count = 0;
	int count = 0;
	int audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
	char *audio_buffer=NULL;
	char sse_hints[128];
	if (NULL == audio_filename)
	{
		printf("params error\n");
		return;
	}

	f_aud=fopen(audio_filename, "rb");
	if (NULL == f_aud)
	{
		printf("audio file open failed! \n\n");
		return;
	}
	fseek(f_aud, 0, SEEK_END);
	audio_size = ftell(f_aud);
	fseek(f_aud, 0, SEEK_SET);
	audio_buffer = (char *)malloc(audio_size);
	if (NULL == audio_buffer)
	{
		printf("malloc failed! \n");
		goto exit;
	}
	real_read = fread((void *)audio_buffer, 1, audio_size, f_aud);
	if (real_read != audio_size)
	{
		printf("read audio file failed!\n");
		goto exit;
	}

	session_id=QIVWSessionBegin(grammar_list, session_begin_params, &err_code);
	if (err_code != MSP_SUCCESS)
	{
		printf("QIVWSessionBegin failed! error code:%d\n",err_code);
		goto exit;
	}

	err_code = QIVWRegisterNotify(session_id, cb_ivw_msg_proc,NULL);
	if (err_code != MSP_SUCCESS)
	{
		_snprintf(sse_hints, sizeof(sse_hints), "QIVWRegisterNotify errorCode=%d", err_code);
		printf("QIVWRegisterNotify failed! error code:%d\n",err_code);
		goto exit;
	}
	while(1)
	{
		long len = 10*FRAME_LEN; //16k音频，10帧 （时长200ms）
		audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if(audio_size <= len)
		{
			len = audio_size;
			audio_stat = MSP_AUDIO_SAMPLE_LAST; //最后一块
		}
		if (0 == audio_count)
		{
			audio_stat = MSP_AUDIO_SAMPLE_FIRST;
		}

		printf("csid=%s,count=%d,aus=%d\n",session_id, count++, audio_stat);
		err_code = QIVWAudioWrite(session_id, (const void *)&audio_buffer[audio_count], len, audio_stat);
		if (MSP_SUCCESS != err_code)
		{
			printf("QIVWAudioWrite failed! error code:%d\n",err_code);
			_snprintf(sse_hints, sizeof(sse_hints), "QIVWAudioWrite errorCode=%d", err_code);
			goto exit;
		}
		if (MSP_AUDIO_SAMPLE_LAST == audio_stat)
		{
			break;
		}
		audio_count += len;
		audio_size -= len;

		//sleep_ms(200); //模拟人说话时间间隙，10帧的音频时长为200ms
	}
	_snprintf(sse_hints, sizeof(sse_hints), "success");

exit:
	if (NULL != session_id)
	{
		QIVWSessionEnd(session_id, sse_hints);
	}
	if (NULL != f_aud)
	{
		fclose(f_aud);
	}
	if (NULL != audio_buffer)
	{
		free(audio_buffer);
	}
}
#endif
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

void start_ivw(const char *grammar_list, const char* audio_filename, const char* session_begin_params)
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

int main(int argc, char* argv[])
{
	int         ret       = MSP_SUCCESS;
	const char *lgi_param = "appid = 5c1e0057, work_dir = .";
	const char *ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";
	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, lgi_param); //第一个参数是用户名，第二个参数是密码，第三个参数是登录参数，用户名和密码可在http://www.xfyun.cn注册获取
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed, error code: %d.\n", ret);
		goto exit ;//登录失败，退出登录
	}
	printf("\n###############################################################################################################\n");
	printf("## 请注意，唤醒语音需要根据唤醒词内容自行录制并重命名为宏IVW_AUDIO_FILE_NAME所指定名称，存放在bin/audio文件里##\n");
	printf("###############################################################################################################\n\n");
	//run_ivw(NULL, IVW_AUDIO_FILE_NAME, ssb_param);	

	//sleep_ms(2000);
	InitializeCriticalSection(&cs);
	init_cycle_buffer();
	start_ivw(NULL, IVW_AUDIO_FILE_NAME, ssb_param);
	
	while (1)
	{

	}
	sleep_ms(2000);
exit:
	printf("按任意键退出 ...\n");
	_getch();
	MSPLogout(); //退出登录
	return 0;
}
