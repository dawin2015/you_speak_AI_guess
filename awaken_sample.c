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
#define FRAME_LEN	640 //16k�����ʵ�16bit��Ƶ��һ֡�Ĵ�СΪ640B, ʱ��20ms

// dawin 20181228 add
boolean win;//�ɼ����ݱ�־λ
FILE *f;//�洢�ɼ�������Ƶ�ļ�
HWAVEIN hWaveIn;  //�����豸
WAVEFORMATEX waveform; //�ɼ���Ƶ�ĸ�ʽ���ṹ��
BYTE *pBuffer1, *pBuffer2;//�ɼ���Ƶʱ�����ݻ���
WAVEHDR wHdr1, wHdr2; //�ɼ���Ƶʱ�������ݻ���Ľṹ��

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
#define BUFFSIZE 1024 * 1024 //���λ������Ĵ�С������Զ����һЩ
#define BUFFER_SIZE 4096


//���λ������ĵ����ݽṹ
struct cycle_buffer {
	char *buf;
	unsigned int size;
	unsigned int in;
	unsigned int out;
};

static struct cycle_buffer *fifo = NULL;//����ȫ��FIFO
FILE *fp;
CRITICAL_SECTION cs;

//��ʼ�����λ�����
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

unsigned int fifo_get(char *buf, unsigned int len)  //�ӻ��λ�������ȡ����
{
	unsigned int l;
	len = min(len, fifo->in - fifo->out);
	l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));
	memcpy(buf, fifo->buf + (fifo->out & (fifo->size - 1)), l);
	memcpy(buf + l, fifo->buf, len - l);
	fifo->out += len;
	return len;
}

unsigned int fifo_put(char *buf, unsigned int len) //�����ݷ��뻷�λ�����
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
	if (MSP_IVW_MSG_ERROR == msg) //���ѳ�����Ϣ
	{
		printf("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
	}
	else if (MSP_IVW_MSG_WAKEUP == msg) //���ѳɹ���Ϣ
	{
		printf("\n\nMSP_IVW_MSG_WAKEUP result = %s\n\n", info);
		// dawin 20181228 add
		win = FALSE;
		//sleep_ms(2000);
		Sleep(2000);
		stop_ivw();
		init_mic(); // �Ժ������������ʶ��
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
		long len = 10*FRAME_LEN; //16k��Ƶ��10֡ ��ʱ��200ms��
		audio_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if(audio_size <= len)
		{
			len = audio_size;
			audio_stat = MSP_AUDIO_SAMPLE_LAST; //���һ��
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

		//sleep_ms(200); //ģ����˵��ʱ���϶��10֡����Ƶʱ��Ϊ200ms
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
//�ص����������ݻ���������ʱ��ͻᴥ�����ص�������ִ�������RecordWave����֮���൱�ڴ�����һ���߳�
DWORD CALLBACK MicCallback(HWAVEIN hwavein, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	int len = 0;
	switch (uMsg)
	{
	//���豸ʱ�����֧��ִ�С�
	case WIM_OPEN:
		printf("\n�豸�Ѿ���...\n");
		break;
	//������������ʱ�������֧��ִ�У���Ҫ�������֧�г���������䣬�ᶪ����	��waveform audio����û�л�����ơ�
	case WIM_DATA:
		printf("\n������%d����...\n", ((LPWAVEHDR)dwParam1)->dwUser);
		if (win)
		{
			
			//�����ٽ���
			EnterCriticalSection(&cs);
			//��������������д�뻷��fifo
			len = fifo_put(((LPWAVEHDR)dwParam1)->lpData, ((PWAVEHDR)dwParam1)->dwBytesRecorded);
			//�˳��ٽ���
			LeaveCriticalSection(&cs);
			waveInAddBuffer(hwavein, (PWAVEHDR)dwParam1, sizeof(WAVEHDR));
		}
		if (record_f != NULL)
		{
			fwrite(((PWAVEHDR)dwParam1)->lpData, ((PWAVEHDR)dwParam1)->dwBytesRecorded, 1, record_f);
		}		
		break;
	case WIM_CLOSE:
		printf("\n�豸�Ѿ��ر�...\n");
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
	waveform.wFormatTag = WAVE_FORMAT_PCM;//������ʽΪPCM
	waveform.nSamplesPerSec = SAMPLERATE;//�����ʣ�16000��/��
	waveform.wBitsPerSample = 16;//�������أ�16bits/��
	waveform.nChannels = 1;//������������1����
	waveform.nAvgBytesPerSec = 16000 * 4;//ÿ��������ʣ�����ÿ���ܲɼ������ֽڵ�����
	waveform.nBlockAlign = 2;//һ����Ĵ�С������bit���ֽ�������������
	waveform.cbSize = 0;//һ��Ϊ0
						//ʹ��waveInOpen����������Ƶ�ɼ�
	MMRESULT mmr = waveInOpen(&hWaveIn, WAVE_MAPPER, &waveform, (DWORD)(MicCallback), NULL, CALLBACK_FUNCTION);
	if (mmr != MMSYSERR_NOERROR)
		return;
	//�����������飨������Խ���������飩����������Ƶ����
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
	//�������õ�wHdr1��Ϊ����
	waveInPrepareHeader(hWaveIn, &wHdr1, sizeof(WAVEHDR));
	wHdr2.lpData = (LPSTR)pBuffer2;
	wHdr2.dwBufferLength = bufsize;
	wHdr2.dwBytesRecorded = 0;
	wHdr2.dwUser = 0;
	wHdr2.dwFlags = 0;
	wHdr2.dwLoops = 1;
	wHdr2.lpNext = NULL;
	wHdr2.reserved = 0;
	//�������õ�wHdr2��Ϊ����
	waveInPrepareHeader(hWaveIn, &wHdr2, sizeof(WAVEHDR));
	//������wHdr��ӵ�waveIn��ȥ
	waveInAddBuffer(hWaveIn, &wHdr1, sizeof(WAVEHDR));
	waveInAddBuffer(hWaveIn, &wHdr2, sizeof(WAVEHDR));
	//��ʼ��Ƶ�ɼ�
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

	fopen_s(&record_f, "¼������.pcm", "ab");
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
	/* �û���¼ */
	ret = MSPLogin(NULL, NULL, lgi_param); //��һ���������û������ڶ������������룬�����������ǵ�¼�������û������������http://www.xfyun.cnע���ȡ
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed, error code: %d.\n", ret);
		goto exit ;//��¼ʧ�ܣ��˳���¼
	}
	printf("\n###############################################################################################################\n");
	printf("## ��ע�⣬����������Ҫ���ݻ��Ѵ���������¼�Ʋ�������Ϊ��IVW_AUDIO_FILE_NAME��ָ�����ƣ������bin/audio�ļ���##\n");
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
	printf("��������˳� ...\n");
	_getch();
	MSPLogout(); //�˳���¼
	return 0;
}
