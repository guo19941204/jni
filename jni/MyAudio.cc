#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#include <map>
#include <string>
#include <list>
#include <pthread.h>    //线程相关

#include "common_types.h"
#include "tick_util.h"
#include "voe_base.h"
#include "voe_codec.h"
#include "voe_file.h"
#include "voe_network.h"
#include "voe_audio_processing.h"
#include "voe_volume_control.h"
#include "voe_hardware.h"
#include "voe_rtp_rtcp.h"
#include "voe_encryption.h"

//#include "com_open_open_cui_myvideo_MyAudio.h"
#include "com_cnambition_ultrasonic_thread_MyAudio.h"

#define WEBRTC_LOG_TAG "*WEBRTCAUDIO* " // As in WEBRTC Native...



/*****************************************************************************************************************/
//音频头，目前没用
/*****************************************************************************************************************/
struct WebrtcAudioHeader
{
	char Codecname[32] = {0};                  //音频编码方式
	unsigned short FS = 0;                      // 音频采样频率
	unsigned short pt = 0;                     //音频采样位数
	unsigned short rate = 0;                  //音频带宽
	unsigned short ch = 0;                     //音频通道数
	unsigned short size = 0;                  //音频数据包大小
	unsigned int IsHaveAudio = false;      //是否有音频包
};

/*****************************************************************************************************************/
//音频包
/*****************************************************************************************************************/
struct AudioPackage
{
    jbyte* pAuData = NULL;
    int nlen = 0;
    AudioPackage()
    {
        pAuData = new jbyte[1000];
        memset(pAuData , '\0' , 1000);
    };
    ~AudioPackage()
    {
        if(nlen != 0 && pAuData != NULL)
        {
            delete[] pAuData;
        }
    }
};

/*****************************************************************************************************************/
//webrtc的音频传输回调,用于获取要发送的音频数据，送到java层发送
/*****************************************************************************************************************/
class VoeExTP : public webrtc::Transport
{
public:
	VoeExTP(int sender_channel, unsigned int localSSRC);
	~VoeExTP();
	virtual int SendPacket(int channel, const void *data, int len);      //rtp包
	virtual int SendRTCPPacket(int channel, const void *data, int len);   //rtcp包
	void EnableN2NTrans(bool bN2N);   //开关多对多传输
    void Init(const std::string& strName);   //初始化
    void Release();  //卸载
    void CleanAuList();    //清空音频发送队列
    jbyteArray SendAnAuPackage(JNIEnv* env);   //发送一个音频包

public:
    std::list<AudioPackage*> m_AudioPackageList;    //音频数据包队列
    pthread_mutex_t m_AudioSenderLock;    //用来发送音频数据的锁

private:
    int m_nOuterAudioChannel = 0;     //从这个通道往外发包
    int m_nRtpPacket = 0;    //rtp包
    int m_nRtcpPacket = 0;   //rtcp包
    WebRtc_UWord32 m_nLocalSSRC = 0;        //内部SSRC
    bool m_bN2N = false;
    std::string m_strLogonName = "";    //登录名
    bool m_bCanUse = false;   //是否能用
};

VoeExTP::VoeExTP(int sender_channel, unsigned int localSSRC)
{
	m_nOuterAudioChannel = sender_channel;        //webrtc的通道号
	m_nLocalSSRC = localSSRC;
}

VoeExTP::~VoeExTP()
{
}

void VoeExTP::EnableN2NTrans(bool bN2N)
{
    m_bN2N = bN2N;
}

int VoeExTP::SendPacket(int channel, const void *data, int len)
{
	++m_nRtpPacket;
	if (channel == m_nOuterAudioChannel && true == m_bN2N && true == m_bCanUse)
	{
        AudioPackage* pTemp = new AudioPackage();
        memcpy(pTemp->pAuData,data,len);//填充pData
        memcpy(pTemp->pAuData+len,m_strLogonName.c_str(),m_strLogonName.length());//填充数据来源用户名
        pTemp->pAuData[len + m_strLogonName.length()]='\0';
        pTemp->nlen = len + 8; //加了本数据的用户名,留了一位加\0
        //重要，向java和c++层共享内存中放数据
        if (0 != pthread_mutex_lock(&m_AudioSenderLock))   //看锁住没有
        {
            delete pTemp;     //没锁住就不要这个包了
            __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onsend lock audiobuffer error!" );
        }
        else    //锁住了，把包放进去
        {
            m_AudioPackageList.push_back(pTemp);
            if(0 != pthread_mutex_unlock(&m_AudioSenderLock))    //解锁
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onsend ulock audiobuffer error!" );
            }
        }
    }
	return len;
}

int VoeExTP::SendRTCPPacket(int channel, const void *data, int len)
{
    return len;
}

void VoeExTP::Init(const std::string& strName)
{
    m_strLogonName = strName;
    int ret = pthread_mutex_init(&m_AudioSenderLock , NULL);    //初始化音频发送锁
    if(ret != 0)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "Unable to init mutex");
        m_bCanUse = false;
    }
    else
    {
        m_bCanUse = true;
    }
}

void VoeExTP::Release()
{
    CleanAuList();
    pthread_mutex_destroy(&m_AudioSenderLock);
}

void VoeExTP::CleanAuList()
{
    if (0 != pthread_mutex_lock(&m_AudioSenderLock))   //看锁住没有
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onclean lock audiobuffer error!" );
    }
    else    //锁住了，把包放进去
    {
        for(auto itr = m_AudioPackageList.begin(); itr != m_AudioPackageList.end(); itr++)
        {
            delete *itr;
        }
        m_AudioPackageList.clear();
        if(0 != pthread_mutex_unlock(&m_AudioSenderLock))    //解锁
        {
            __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onclean ulock audiobuffer error!" );
        }
    }
}

jbyteArray VoeExTP::SendAnAuPackage(JNIEnv* env)
{
    AudioPackage* pPackage = NULL;
    if (0 != pthread_mutex_lock(&m_AudioSenderLock))   //看锁住没有
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onSend lock audiobuffer error!" );
        return NULL;
    }
    if(m_AudioPackageList.size() <= 0)   //没数据
    {
        if(0 != pthread_mutex_unlock(&m_AudioSenderLock))    //解锁
        {
            __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onSend ulock audiobuffer error!" );
        }
        return NULL;
    }
    pPackage = m_AudioPackageList.front();   //拿到包
    m_AudioPackageList.pop_front();    //扔了
    if(0 != pthread_mutex_unlock(&m_AudioSenderLock))    //拿完了就解锁
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "onSend ulock audiobuffer error!" );
    }
    jbyteArray dataToSend = env->NewByteArray(pPackage->nlen);    //让java去new一个
    env->SetByteArrayRegion(dataToSend , 0 , pPackage->nlen , pPackage->pAuData);    //往里面放数据
    delete pPackage;
    return dataToSend;
}
/*****************************************************************************************************************/
//用于多对多时接收音频数据
/*****************************************************************************************************************/
class VoeN2NExTP : public webrtc::Transport
{
public:
	VoeN2NExTP();
	~VoeN2NExTP();
	virtual int SendPacket(int channel, const void *data, int len);            //多对多时只接收别人发过来的数据
	virtual int SendRTCPPacket(int channel, const void *data, int len);

public:
	int nChannel;    //接收音频数据的通道
	bool bPlaying;      //是该通道在播放
	bool bReceiving;    //是否该通道在接收
	int nRtpPackNum;     //接收的rtp包的数量
	int nRtcpPackNum;    //接收的rtcp包的数量
};

VoeN2NExTP::VoeN2NExTP()
{
    nChannel = -1;
    bPlaying = false;
    bReceiving = false;
    nRtpPackNum = 0;
    nRtcpPackNum = 0;
}

VoeN2NExTP::~VoeN2NExTP()
{
}

int VoeN2NExTP::SendPacket(int channel, const void *data, int len)            //多对多时只接收别人发过来的数据
{
    return len;
}
int VoeN2NExTP::SendRTCPPacket(int channel, const void *data, int len)
{
    return len;
}


/*****************************************************************************************************************/
//音频处理类
/*****************************************************************************************************************/
class CWebRtcAudioStream
{
public:
	CWebRtcAudioStream();
	~CWebRtcAudioStream();

	bool Init(bool enableWebrtcAuLog, JNIEnv* pJniEnv, jobject context);              //初始化
    void InitFalse();
	void Release();       //销毁
    bool CreateChannel();  //创建音频通道
	bool SetupCodecSetting(const int idx);      //按给定的序号设置音频编解码
	bool SetupExSender(const std::string& strName);              //设置传输回调
	void ReleaseExSender();					//卸载传输回调
	bool StartSend();       //开始传输，对于音频引擎，开始传输的同时也开始录音,必须先设置扩展传输类
	bool StopSend();      //停止传输，停止了传输之后别人也听不到你的声音了
	bool StartAudioPlayout();     //开始播放
	bool StopAudioPlayout();     //停止播放
	bool StartReceive();     //开始接收
	bool StopReceive();     //停止接收
	bool ReceivedN2NRtpData(char* pDataBuffer, int nLen);      //主动接收rtp包
	void EnableAllN2NReceiving(bool bAllN2N);       //允许多对多接收
	bool AddN2NUser(const char* userID);         //添加一个用户，发过来的为该用户对应的服务器id
	bool ReleaseN2NUser(const char* username);      //删除一个用户
	void ReleaseN2NUsers();         //删除所有用户
    void SetJavaVM(JavaVM* pVM);     //设置java虚拟机
    jbyteArray SendAnAuPackage(JNIEnv* env);    //发送一个音频包


public:
	//webrtc引擎接口
	webrtc::VoiceEngine*			m_pVoeEngine;             //webrtc音频引擎
	webrtc::VoEBase*				m_pVoeBase;				//音频引擎的一些基本接口
	webrtc::VoECodec*				m_pVoeCodec;				//编解码
	webrtc::VoEEncryption*		    m_pVoeEncrypt;			//加解密
	webrtc::VoEHardware*            m_pVoeHardware;			//音频硬件
	webrtc::VoENetwork*             m_pVoeNetwork;				//网络
	webrtc::VoERTP_RTCP*            m_pVoeRtpRtcp;				//RtpRtcp协议
	webrtc::VoEVolumeControl*       m_pVoeVolumeControl;      //音量控制
	webrtc::VoEAudioProcessing*     m_pVoe_apm;           //音频处理
	int                             m_nAudioChannel;     //视频通道，用来输出采集的数据，也可以同时接收网络过来的数据
    WebrtcAudioHeader  audioHeader;

private:
    JavaVM* m_pJvm;    //java虚拟机可以保存
	webrtc::CodecInst m_CodecInst;     //voe的编码
	bool m_bInit;      //是否初始化成功
	VoeExTP* m_pSender;     //传输时的回调
	bool m_bSending;    //是否在发送数据
	bool m_bReceiving;  //是否在接收数据，接收p2p
	bool m_bRecording;    //是否录音本地数据
	bool m_bReceivingN2N;   //n2n
	unsigned int m_nRtpPackNum;     //接收到的rtp包的数量
	unsigned int m_nRtcpPackNum;    //接收到的rtcp包的数量
	unsigned int m_nFirstRTPRecvSSRC;     //接收过来的第一个rtp包的ssrc
	std::map<std::string,VoeN2NExTP*> m_Userlist;       //正在与该用户进行多对多的用户
};

/*****************************************************************************************************************/
//CWebRtcAudioStream 类的实现
/*****************************************************************************************************************/

 //默认构造
CWebRtcAudioStream::CWebRtcAudioStream()
{
    m_pJvm = NULL;
	m_pVoeEngine = NULL;
	m_pVoeBase = NULL;
	m_pVoeCodec = NULL;
	m_pVoeEncrypt = NULL;
	m_pVoeHardware = NULL;
	m_pVoeNetwork = NULL;
	m_pVoeRtpRtcp = NULL;
	m_pVoeVolumeControl = NULL;
	m_pVoe_apm = NULL;
	m_bInit = false;
	m_nAudioChannel = -1;
	m_bSending = false;
	m_bReceiving = false;
	m_bRecording = false;
	m_bReceivingN2N = false;
	m_nRtpPackNum = 0;
	m_nRtcpPackNum = 0;
	m_nFirstRTPRecvSSRC = 999;
	m_pSender = NULL;
}


//析构
CWebRtcAudioStream::~CWebRtcAudioStream()
{
	if(m_bInit)          //如果之前没有卸载过，现在就卸载
    {
        Release();
    }
	if(m_pSender)
    {
        delete m_pSender;
    }
}


//初始化
bool CWebRtcAudioStream::Init(bool enableWebrtcAuLog, JNIEnv* pJniEnv, jobject context)
{
    if(NULL == m_pJvm)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "之前获取JAVA虚拟机失败，无法初始化!");
        return false;
    }
    webrtc::VoiceEngine::SetAndroidObjects(m_pJvm, pJniEnv, context);  //能进到这里说明load
    int nRet = -1;    //音频引擎各模块初始化结果
    m_pVoeEngine = webrtc::VoiceEngine::Create();        //webrtc的视频引擎全局接口
    if (!m_pVoeEngine)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "初始化VoeEngine失败!");
        m_bInit = false;
        return m_bInit;
    }
    if (enableWebrtcAuLog)   //是否启用内部日志
    {
        if(m_pVoeEngine->SetTraceFile("/sdcard/trace.txt"))   //音频引擎内部日志文件名，调试用，函数成功返回0，不成功返回-1
        {
            __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "设置VoeEngine的TraceFile出错!");
        }
        if(m_pVoeEngine->SetTraceFilter(
                webrtc::kTraceError |
                webrtc::kTraceWarning |
                webrtc::kTraceCritical))      // enum TraceLevel 定义了监视那些行为
        {
            __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "设置VoeEngine的TraceFilter出错!");
        }
    }
    else
    {
        m_pVoeEngine->SetTraceFile(NULL);
        m_pVoeEngine->SetTraceFilter(webrtc::kTraceNone);
    }

    m_pVoeBase = webrtc::VoEBase::GetInterface(m_pVoeEngine);      //VoeBase用来创建通道，传输
    if (!m_pVoeBase)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeBase失败!");
        m_bInit = false;
        return m_bInit;
    }
    if(m_pVoeBase->Init())             //初始化VoeBase
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "初始化VoeBase失败! 错误码 %d\n" , m_pVoeBase->LastError());
        m_bInit = false;
        return m_bInit;
    }
    m_pVoeCodec = webrtc::VoECodec::GetInterface(m_pVoeEngine);          //编解码
    if (!m_pVoeCodec)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeCodec失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoeEncrypt = webrtc::VoEEncryption::GetInterface(m_pVoeEngine);            //加解密
    if (!m_pVoeEncrypt)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeEncrypt失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoeHardware = webrtc::VoEHardware::GetInterface(m_pVoeEngine);            //音频硬件
    if (!m_pVoeHardware)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeHardware失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoeNetwork = webrtc::VoENetwork::GetInterface(m_pVoeEngine);            //网络
    if (!m_pVoeNetwork)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeNetwork失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoeRtpRtcp = webrtc::VoERTP_RTCP::GetInterface(m_pVoeEngine);            //rtcp协议
    if (!m_pVoeRtpRtcp)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoERTP_RTCP失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoeVolumeControl = webrtc::VoEVolumeControl::GetInterface(m_pVoeEngine);            //音量控制
    if (!m_pVoeVolumeControl)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取VoeVolumeControl失败!");
        m_bInit = false;
        return m_bInit;
    }

    m_pVoe_apm = webrtc::VoEAudioProcessing::GetInterface(m_pVoeEngine);            //音频处理
    if (!m_pVoe_apm)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "获取Voe_apm失败!");
        m_bInit = false;
        return m_bInit;
    }

  //  __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "EcStatus kEcConference... ret %d errcode %d\n" , nRet , m_pVoeBase->LastError());
    // nRet = m_pVoe_apm->SetNsStatus(true , webrtc::kNsConference);     //噪音控制，设置成超高噪音抑制
    // nRet = m_pVoe_apm->SetAgcStatus(true);
   // __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "NsStatus kNsConference... ret %d errcode %d\n" , nRet , m_pVoeBase->LastError());
    //nRet = m_pVoe_apm->SetAgcStatus(true , webrtc::kAgcAdaptiveAnalog);     //Automatic Gain Control 自动增益控制 ，设成默认
   //  nRet = m_pVoe_apm->SetAgcStatus(true);
  //  __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "AgcStatus kAgcDefault... ret %d errcode %d\n" , nRet , m_pVoeBase->LastError());

    //nRet = m_pVoe_apm->SetEcStatus(true , webrtc::kEcConference);     //回声控制，设置成会议类型
    nRet = m_pVoe_apm->SetEcStatus(true , webrtc::kEcAecm);     //回声控制，aecm
   // nRet = m_pVoe_apm->SetEcStatus(true);     //回声控制，aecm
     m_bInit = true;
    return m_bInit;
}


//强制初始化不成功
void CWebRtcAudioStream::InitFalse()
{
    m_bInit = false;
}


//卸载
void CWebRtcAudioStream::Release()
{
    //先卸载多对多
    ReleaseN2NUsers();
    ReleaseExSender();
    //最后才卸载引擎
    if (m_pVoe_apm)
    {
        m_pVoe_apm->Release();
        m_pVoe_apm = NULL;
    }
    if (m_pVoeVolumeControl)
    {
        m_pVoeVolumeControl->Release();
        m_pVoeVolumeControl = NULL;
    }
    if (m_pVoeRtpRtcp)
    {
        m_pVoeRtpRtcp->Release();
        m_pVoeRtpRtcp = NULL;
    }
    if (m_pVoeNetwork)
    {
        m_pVoeNetwork->Release();
        m_pVoeNetwork = NULL;
    }
    if (m_pVoeHardware)
    {
        m_pVoeHardware->Release();
        m_pVoeHardware = NULL;
    }
    if (m_pVoeEncrypt)
    {
        m_pVoeEncrypt->Release();
        m_pVoeEncrypt = NULL;
    }
    if (m_pVoeCodec)
    {
        m_pVoeCodec->Release();
        m_pVoeCodec = NULL;
    }
    if (m_pVoeBase)
    {
        m_pVoeBase->DeleteChannel(m_nAudioChannel);
        m_pVoeBase->Terminate();
        m_pVoeBase->Release();
        m_pVoeBase = NULL;
    }
    if (m_pVoeEngine)
    {
        webrtc::VoiceEngine::Delete(m_pVoeEngine);
        m_pVoeEngine = NULL;
    }
    m_bInit = false;
}


bool CWebRtcAudioStream::CreateChannel()
{
    if (!m_bInit)
    {
        return false;
    }
    m_nAudioChannel = m_pVoeBase->CreateChannel();         //创建通道
    if (m_nAudioChannel == -1)
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "创建音频通用通道! 错误码 %d\n" , m_pVoeBase->LastError());
        return false;
    }
    return true;
}
 //ISAC  fs= 16000, pt= 103, rate=  32000, ch= 1, size=  480
 //ISAC  fs= 32000, pt= 104, rate=  56000, ch= 1, size=  960
 //PCMU  fs=  8000, pt=   0, rate=  64000, ch= 1, size=  160
 //PCMA  fs=  8000, pt=   8, rate=  64000, ch= 1, size=  160
 //PCMU  fs=  8000, pt= 110, rate=  64000, ch= 2, size=  160
 //PCMA  fs=  8000, pt= 118, rate=  64000, ch= 2, size=  160
 //ILBC  fs=  8000, pt= 102, rate=  13300, ch= 1, size=  240
 //CN  fs=  8000, pt=  13, rate=      0, ch= 1, size=  240
 //CN  fs= 16000, pt=  98, rate=      0, ch= 1, size=  480
 //CN  fs= 32000, pt=  99, rate=      0, ch= 1, size=  960
 //CN  fs= 48000, pt= 100, rate=      0, ch= 1, size= 1440
//按给定的序号设置音频编解码
bool CWebRtcAudioStream::SetupCodecSetting(const int idx)
{
    if (!m_bInit)
    {
        return false;
    }
    int nCodecs = m_pVoeCodec->NumOfCodecs();     //当前有几种编码
    //编号从0开始
    //ISAC  fs= 16000, pt= 103, rate=  32000, ch= 1, size=  480
    //ISAC  fs= 32000, pt= 104, rate=  56000, ch= 1, size=  960
    //PCMU  fs=  8000, pt=   0, rate=  64000, ch= 1, size=  160
    //PCMA  fs=  8000, pt=   8, rate=  64000, ch= 1, size=  160
    //PCMU  fs=  8000, pt= 110, rate=  64000, ch= 2, size=  160
    //PCMA  fs=  8000, pt= 118, rate=  64000, ch= 2, size=  160
    //ILBC  fs=  8000, pt= 102, rate=  13300, ch= 1, size=  240
    //CN  fs=  8000, pt=  13, rate=      0, ch= 1, size=  240
    //CN  fs= 16000, pt=  98, rate=      0, ch= 1, size=  480
    //CN  fs= 32000, pt=  99, rate=      0, ch= 1, size=  960
    //CN  fs= 48000, pt= 100, rate=      0, ch= 1, size= 1440
    for (int index = 0; index < nCodecs; index++)
    {
        if (index == idx)
        {
            m_pVoeCodec->GetCodec(index, m_CodecInst);
            m_pVoeCodec->SetSendCodec(m_nAudioChannel , m_CodecInst);
            __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "已指定的编码为: %s  fs=%6d, pt=%4d, rate=%7d, ch=%2d, size=%5d\n",
                                m_CodecInst.plname , m_CodecInst.plfreq , m_CodecInst.pltype , m_CodecInst.rate , m_CodecInst.channels , m_CodecInst.pacsize);
            memcpy(audioHeader.Codecname,m_CodecInst.plname,RTP_PAYLOAD_NAME_SIZE);
            audioHeader.FS=m_CodecInst.plfreq;
            audioHeader.pt=m_CodecInst.pltype;
            audioHeader.rate=m_CodecInst.rate;
            audioHeader.ch=m_CodecInst.channels;
            audioHeader.size=m_CodecInst.pacsize;
            audioHeader.IsHaveAudio=1;
            return true;
        }
    }
    __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "当前编码个数为%d，找不到第%d个指定编码!\n", nCodecs, idx);
    return true;
}


//设置传输回调
bool CWebRtcAudioStream::SetupExSender(const std::string& strName)
{
    ReleaseExSender();     //先卸载
    if(!m_bInit)
    {
        return false;
    }
    if (m_pSender == NULL)
    {
        srand((int) webrtc::TickTime::MicrosecondTimestamp());    //随机种子
        unsigned int localSSRC = (unsigned int)(rand() % 100);       //随机100中的一个数作为rtp包内部校验码
        int ret = m_pVoeRtpRtcp->SetLocalSSRC(m_nAudioChannel , localSSRC);
        __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "VoeRtpRtcp SetLocalSSRC ssrc %d ret %d errcode %d\n" ,
                            localSSRC , ret , m_pVoeBase->LastError());
        m_pSender = new VoeExTP(m_nAudioChannel , localSSRC);
        ret = m_pVoeNetwork->RegisterExternalTransport(m_nAudioChannel , *m_pSender);     //设置传输回调
        __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "已设置扩展传输类 channel %d ret %d errcode %d..\n" ,
                            m_nAudioChannel , ret , m_pVoeBase->LastError());
        ret = m_pVoeRtpRtcp->SetRTCPStatus(m_nAudioChannel , true);      //开启rtp状态
        __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "VoeRtpRtcp SetRTCPStatus channel %d ret %d errcode %d\n" ,
                            m_nAudioChannel , ret , m_pVoeBase->LastError());
        ret = m_pVoeRtpRtcp->SetRTPAudioLevelIndicationStatus(m_nAudioChannel, true);
        __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "VoeRtpRtcp SetRTPAudioLevelIndicationStatus channel %d ret %d errcode %d\n" ,
                            m_nAudioChannel , ret , m_pVoeBase->LastError());
        m_pVoeCodec->SetSendCNPayloadType(m_nAudioChannel , m_CodecInst.pltype);   //设置噪音抑制
        ret = m_pVoeCodec->SetVADStatus(m_nAudioChannel, false);           //设置静音忽略和断续传输
        __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "VoeCodec SetVADStatus channel %d ret %d errcode %d\n" ,
                            m_nAudioChannel , ret , m_pVoeBase->LastError());
        m_pSender->Init(strName);
        m_pSender->EnableN2NTrans(false);
    }
    return true;
}


//卸载传输回调
void CWebRtcAudioStream::ReleaseExSender()
{
    if (!m_bInit)
    {
        return;
    }
    if (m_bSending)      //必须先停止发送或接收
    {
        StopSend();
    }
    if (m_bReceiving)
    {
        StopReceive();
    }
    if(m_pSender)
    {
        m_pVoeNetwork->DeRegisterExternalTransport(m_nAudioChannel);
        m_pSender->Release();
        delete m_pSender;
        m_pSender = NULL;
    }
    m_pVoeRtpRtcp->SetRTCPStatus(m_nAudioChannel , false);    //关rtp状态
    m_pVoeRtpRtcp->SetRTPAudioLevelIndicationStatus(m_nAudioChannel, false);
    m_pVoeCodec->SetVADStatus(m_nAudioChannel, false);
    m_nFirstRTPRecvSSRC = 999;
    m_nRtpPackNum = 0;     //接收到的rtp包的数量
    m_nRtcpPackNum = 0;    //接收到的rtcp包的数量
}


//开始播放
bool CWebRtcAudioStream::StartAudioPlayout()
{
    if (m_bInit)     //确保初始化了才开始采集
    {
        if(!m_bRecording)
        {
            int ret = -1;
            ret = m_pVoeBase->StartPlayout(m_nAudioChannel);
            m_bRecording = true;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "开始播放声音失败 channel %d ret %d errcode %d\n",
                                    m_nAudioChannel , ret , m_pVoeBase->LastError());
            }
        }
        return true;    //若开始成功则ret为0
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase未初始化，无法开始播放声音...");
    return false;
}


//停止播放
bool CWebRtcAudioStream::StopAudioPlayout(void)
{
    if (m_bInit)     //正在播
    {
        if (m_bRecording)
        {
            int ret = -1;
            m_pVoeBase->StopPlayout(m_nAudioChannel);
            m_bRecording = false;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "停止播放声音失败 channel %d ret %d errcode %d\n",
                                    m_nAudioChannel , ret , m_pVoeBase->LastError());
            }
        }
        return true;
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase未初始化，无法停止播放声音...");
    return false;
}


//开始传输，对于音频引擎，开始传输的同时也开始录音,必须先设置扩展传输类
bool CWebRtcAudioStream::StartSend(void)
{
    if (!m_pSender)
    {
        return false;
    }
    if(m_bInit)
    {
        if (!m_bSending)        //如果没有发送才发送
        {
            int ret = -1;
            ret = m_pVoeBase->StartSend(m_nAudioChannel);
            m_bSending = true;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "开始发送音频失败! ret %d errcode %d\n",
                                    ret , m_pVoeBase->LastError());
            }
            if(NULL != m_pSender)
            {
                m_pSender->EnableN2NTrans(true);    //允许往发送队列里面添加数据了
            }
        }
        return true;
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase未初始化，无法发送音频...");
    return false;
}


//停止传输，停止了传输之后别人也听不到你的声音了
bool CWebRtcAudioStream::StopSend(void)
{
    if (m_bInit)
    {
        if(m_bSending)       //如果已经在发送才停止
        {
            int ret = -1;
            if(NULL != m_pSender)
            {
                m_pSender->EnableN2NTrans(false);   //停止往发送队列避免添加数据
            }
            ret = m_pVoeBase->StopSend(m_nAudioChannel);     //先停止发送
            m_bSending = false;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "停止发送音频失败! ret %d errcode %d\n",
                                    ret , m_pVoeBase->LastError());
            }
            if(NULL != m_pSender)
            {
                m_pSender->CleanAuList();   //清空队列
            }
        }
        return true;
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase尚未初始化，无法停止发送!");
    return false;
}


//开始接收
bool CWebRtcAudioStream::StartReceive(void)
{
    if (!m_pSender)
    {
        return false;
    }
    if (m_pVoeBase)
    {
        if(!m_bReceiving)            //没有接收才开始接收
        {
            int ret = -1;
            ret = m_pVoeBase->StartReceive(m_nAudioChannel);
            m_bReceiving = true;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "开始接收音频数据失败! channel %d ret %d errcode %d\n",
                                    m_nAudioChannel , ret , m_pVoeBase->LastError());
            }
        }
        return true;
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase尚未初始化，无法开始接收!");
    return false;
}


//停止接收
bool CWebRtcAudioStream::StopReceive(void)
{
    if (m_pVoeBase)
    {
        if (m_bReceiving)
        {
            int ret = -1;
            ret = m_pVoeBase->StopReceive(m_nAudioChannel);
            m_bReceiving = false;
            if(ret != 0)
            {
                __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "停止接收音频数据失败! channel %d ret %d errcode %d\n",
                                    m_nAudioChannel , ret , m_pVoeBase->LastError());
            }
        }
        return true;
    }
    __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "VoeBase尚未初始化，无法停止接收!");
    return false;
}


//主动接收rtp包
bool CWebRtcAudioStream::ReceivedN2NRtpData(char* pDataBuffer, int nLen)
{
    if (!m_bInit)
    {
        return false;
    }
    if (!m_bReceivingN2N)      //如果不要求接收n2n的数据，则退出
    {
        return false;
    }
    bool rettt = false;
    char FromUser[64];//有一位是\0
    memcpy(FromUser,pDataBuffer+(nLen - 8), 8);
    auto itr = m_Userlist.find(std::string(FromUser));
    if (itr != m_Userlist.end())
    {
        if (itr->second->bReceiving)
        {
            itr->second->nRtpPackNum++;      //对应该用户的rtp包增加
            int ret = m_pVoeNetwork->ReceivedRTPPacket(itr->second->nChannel , pDataBuffer , nLen-8);
            /*WriteLogType(AUDO_VIDEO_LOG, "用户 %s对应的channel %d 接收到第 %d个rtp包，长度 %d ret %d errcode %d\n" ,
                username , itr->second->nChannel , itr->second->nRtpPackNum , len , ret , m_pVoeBase->LastError());*/
            rettt = true;
        }
    }
    return rettt;
}


//允许多对多接收
void CWebRtcAudioStream::EnableAllN2NReceiving(bool bAllN2N)
{
    if (m_bInit)
    {
        m_bReceivingN2N = bAllN2N;
    }
}


//添加一个用户
bool CWebRtcAudioStream::AddN2NUser(const char* userID)
{
    if (userID == NULL)
    {
        return false;
    }
    if (!m_bInit)
    {
        return false;
    }
    std::string strUserID = userID;
    if (m_Userlist.find(strUserID) != m_Userlist.end())
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "已经在接收用户---服务器id【%s】，不要重复添加!\n", strUserID.c_str());
        return false;
    }
    VoeN2NExTP* newUser = new VoeN2NExTP();
    newUser->nChannel = m_pVoeBase->CreateChannel();         //创建通道
    if (newUser->nChannel == -1)
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "为用户服务器id【%s】设置音频通道出错! 错误码 %d\n" ,
                            strUserID.c_str(), m_pVoeBase->LastError());
    }
    int ret = m_pVoeNetwork->RegisterExternalTransport(newUser->nChannel , *newUser);     //设置传输回调
    __android_log_print(ANDROID_LOG_WARN, WEBRTC_LOG_TAG, "已为用户---服务器id【%s】设置音频扩展传输类 channel %d ret %d errcode %d..\n",
                        strUserID.c_str() , newUser->nChannel , ret , m_pVoeBase->LastError());
    ret = m_pVoeRtpRtcp->SetRTCPStatus(newUser->nChannel , true);      //开启rtp状态
    ret = m_pVoeRtpRtcp->SetRTPAudioLevelIndicationStatus(newUser->nChannel, true);
    ret = m_pVoeCodec->SetVADStatus(newUser->nChannel, true);           //设置静音忽略和断续传输
    ret = m_pVoeBase->StartPlayout(newUser->nChannel);    //创建了该用户就开始播放
    newUser->bPlaying = true;
    ret = m_pVoeBase->StartReceive(newUser->nChannel);     //创建了该用户就开始接收
    newUser->bReceiving = true;
    m_Userlist.insert(std::map<std::string,VoeN2NExTP*>::value_type(strUserID , newUser));   //把该用户添加到用户列表中
    return true;
}


//删除一个用户
bool CWebRtcAudioStream::ReleaseN2NUser(const char* username)
{
    if (!m_bInit)
    {
        return false;
    }
    auto itr = m_Userlist.find(std::string(username));
    if (itr == m_Userlist.end())
    {
        __android_log_print(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "没有此用户%s，不能删除!\n", username);
        return false;
    }
    if (itr->second->bPlaying)         //停止播放
    {
        m_pVoeBase->StopPlayout(itr->second->nChannel);
    }

    if (itr->second->bReceiving)         //停止接收
    {
        m_pVoeBase->StopReceive(itr->second->nChannel);
    }
    m_pVoeNetwork->DeRegisterExternalTransport(itr->second->nChannel);     //卸载传输回调
    m_pVoeRtpRtcp->SetRTCPStatus(itr->second->nChannel , false);    //关rtp状态
    m_pVoeRtpRtcp->SetRTPAudioLevelIndicationStatus(itr->second->nChannel, false);
    m_pVoeCodec->SetVADStatus(itr->second->nChannel, false);
    m_pVoeBase->DeleteChannel(itr->second->nChannel);
    delete itr->second;           //删传输回调
    itr->second = NULL;
    m_Userlist.erase(itr);      //删掉该用户
    return true;
}


//删除所有用户
void CWebRtcAudioStream::ReleaseN2NUsers(void)
{
    if (!m_bInit)
    {
        return;
    }
    for(auto itr = m_Userlist.begin(); itr != m_Userlist.end(); itr++)
    {
        if (itr->second->bPlaying)         //停止播放
            m_pVoeBase->StopPlayout(itr->second->nChannel);
        if (itr->second->bReceiving)         //停止接收
            m_pVoeBase->StopReceive(itr->second->nChannel);
        m_pVoeNetwork->DeRegisterExternalTransport(itr->second->nChannel);     //卸载传输回调
        m_pVoeRtpRtcp->SetRTCPStatus(itr->second->nChannel , false);    //关rtp状态
        m_pVoeRtpRtcp->SetRTPAudioLevelIndicationStatus(itr->second->nChannel, false);
        m_pVoeCodec->SetVADStatus(itr->second->nChannel, false);
        delete itr->second;           //删传输回调
        itr->second = NULL;
    }
    m_Userlist.clear();
}


//设置java虚拟机
void CWebRtcAudioStream::SetJavaVM(JavaVM* pVM)
{
    m_pJvm = pVM;
}

//发送一个音频包
jbyteArray CWebRtcAudioStream::SendAnAuPackage(JNIEnv* env)
{
    if(m_bInit)
    {
        return m_pSender->SendAnAuPackage(env);
    }
    return NULL;
}

/*****************************************************************************************************************/
//java nativa 函数的实现
/*****************************************************************************************************************/
//全局变量
static CWebRtcAudioStream g_WebRtcVoe;
static char g_szUserId[64];   //用来转换用户名
static jbyte auBufContainer[2000];    //用来装音频数据的容器



/////////////////////////////////////////////
// JNI_OnLoad
//java类加载jni，如System.loadLibrary("xxxxx");要用到，这个很重要
jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
    if (!vm)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "加载JNI的时候没有获取到可用的虚拟机!!");
        return -1;
    }
    g_WebRtcVoe.SetJavaVM(vm);
    return JNI_VERSION_1_4;
}

/////////////////////////////////////////////
// Native initialization
//
JNIEXPORT jboolean JNICALL
Java_com_cnambition_ultrasonic_thread_MyAudio_NativeInit(
        JNIEnv * env,
        jclass context)
{
    return true;
}

//初始化
//enableWebrtcAuLog 是否启用webrtc内部日志
//logonName 登录名
//sendBuf 从java层获取的可擦鞋的发送缓冲
JNIEXPORT jboolean JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_Init(
        JNIEnv* env,
        jobject context,
        jboolean enableWebrtcAuLog,
        jstring logonName)
{

    if (false == g_WebRtcVoe.Init(enableWebrtcAuLog , env , context))
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "webrtc音频初始化失败！");
        return false;
    }
    if(false == g_WebRtcVoe.CreateChannel())
    {
        g_WebRtcVoe.InitFalse();
        return false;
    }
    jsize len = (*env).GetStringLength(logonName);  //java传过来的默认字符串是Unicode的，获取unicode字符串的长度
    if(len <= 0)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "本机登录用户名为空!");
        g_WebRtcVoe.InitFalse();
        return false;
    }
    memset(g_szUserId, '\0' , 64);
    (*env).GetStringUTFRegion(logonName,0,len,g_szUserId);   //转成uft8格式拷给c++的buffer，不分配内存空间方式的拷贝，安全
    std::string temp = g_szUserId;
    g_WebRtcVoe.SetupExSender(temp);
    //[ 3]PCMA: fs=  8000, pt=   8, rate=  64000, ch= 1, size=  160
    if (false == g_WebRtcVoe.SetupCodecSetting(3))
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "webrtc音频设置编码方式失败!\n");
        g_WebRtcVoe.audioHeader.IsHaveAudio=0;
        g_WebRtcVoe.InitFalse();
        return false;
    }
    g_WebRtcVoe.StopAudioPlayout();
    g_WebRtcVoe.StopSend();    //不允许传输回调
    g_WebRtcVoe.EnableAllN2NReceiving(false);   //初始化音频为false
    return true;
}

//卸载
JNIEXPORT void JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_Release(
        JNIEnv* env,
        jobject context)
{
    g_WebRtcVoe.EnableAllN2NReceiving(false);
    g_WebRtcVoe.StopSend();
    g_WebRtcVoe.StopAudioPlayout();
    g_WebRtcVoe.Release();
}

//通话组变动，有人加入或退出
JNIEXPORT void JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_AddOrReleaseUser(
        JNIEnv* env,
        jobject context,
        jstring uid,
        jboolean bAdd)
{
    memset(g_szUserId, '\0' , 64);
    jsize len = (*env).GetStringLength(uid);  //java传过来的默认字符串是Unicode的，获取unicode字符串的长度
    if(len <= 0)
    {
        __android_log_write(ANDROID_LOG_ERROR, WEBRTC_LOG_TAG, "没有获取到用户名!");
        return;
    }
    (*env).GetStringUTFRegion(uid,0,len,g_szUserId);   //转成uft8格式拷给c++的buffer，不分配内存空间方式的拷贝，安全
    if(true == bAdd)    //新加入的用户
    {
        g_WebRtcVoe.AddN2NUser(g_szUserId);
    }
    else   //这个用户退了
    {
        g_WebRtcVoe.ReleaseN2NUser(g_szUserId);
    }
}

//通话组发生变动，通话组不为空
JNIEXPORT void JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_ClientingListNonEmpty(
        JNIEnv* env,
        jobject context)
{
    g_WebRtcVoe.StartAudioPlayout();   //开始播音
    g_WebRtcVoe.EnableAllN2NReceiving(true);    //允许接收
    g_WebRtcVoe.StartSend();    //开始发送
}

//通话组发生变动，通话组为空了
JNIEXPORT void JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_ClientingListEmpty
        (JNIEnv* env,
         jobject context)
{
    g_WebRtcVoe.StopSend();   //关闭发送
    g_WebRtcVoe.EnableAllN2NReceiving(false);  //停止接收
    g_WebRtcVoe.ReleaseN2NUsers();
    g_WebRtcVoe.StopAudioPlayout();    //停止播音
}

//接收音频数据
JNIEXPORT void JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_ReceiveAuBuffer(
        JNIEnv* env,
        jobject context,
        jbyteArray auBuffer,
        jint nlen)
{
    (*env).GetByteArrayRegion(auBuffer , 0 , nlen , auBufContainer); //拷贝Java数组中的所有元素到缓冲区中，没有内存分配，安全
    g_WebRtcVoe.ReceivedN2NRtpData((char*)auBufContainer , nlen);
}

//发音频数据给java
JNIEXPORT jbyteArray JNICALL Java_com_cnambition_ultrasonic_thread_MyAudio_GetAuSendData(
        JNIEnv* env,
        jobject context)
{
    return g_WebRtcVoe.SendAnAuPackage(env);
}