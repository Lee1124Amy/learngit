#include "config.h"
#include "protocol.h"
#include "uart_socket.h"
#include "rtc_api.h"
#include <time.h>
#include "other/outlet/outlet.h"
/*
������б��жϺ���ʹ��32λ����ʱ��stm8��Ƭ���������⣡��
*/
uint16 task_monitor = 0;
/* 
        bit0~bit6: event type   bit0:      relay status change
                                bit1:      adjust light 
        bit7:      event report flag
*/
//DATA_SEG struct EVENT_INFOR  event;
extern outlet_state outletstateA;
extern outlet_state outletstateB;
uint8 g_frame_buffer[MAX_BUFFER_SZ];//
struct EEP_PARAM   eep_param;
struct REG reg;
struct PLC_MACHINE  plc_state;
extern uart_socket_t *uart_socket;

static uint8 rst_plc_t = 0;
#define clear_rst_time(pframe)        do{if(CMD_ACK_EID == pframe->infor[0]) rst_plc_t = 0;}while(0)

#define MAX_OVERTIME    (plc_state.trycnt+5)
#define MAX_TRYCNT      2
#define MAX_RST_TIME    3

void chg_state(uint8 cur_state);
#define next_state()         do{chg_state(plc_state.pstate->next_state);}while(0)
#define try_state_again()    do{chg_state(plc_state.pstate->cur_state); plc_state.trycnt++;}while(0)

static uint8 local_ack_opt(struct SHS_frame *pframe)
{
    if(pframe != NULL)
    {
        if((0x00 == is_all_xx(pframe->said, 0x00, ID_LEN))
           || (0x00 == is_all_xx(pframe->taid, 0x00, ID_LEN)))
        {
            return(0);
        }
        if((CMD_ACK == pframe->infor[0]) || (CMD_NAK == pframe->infor[0]))
        {
            next_state();
            return(1);
        }
    }
    if(plc_state.wait_t > MAX_OVERTIME)
    {
//        if(++plc_state.trycnt > MAX_TRYCNT) 
//        {
//            chg_state(RST_PLC);
//            return(0);
//        }
        try_state_again();
    }
    return(0);
}
#define     MIN_PLC_RUN        2
static uint8 reset_plc(uint8 init, void *args)
{
#if 0
    if(init)
    {
        SET_PIN_L(PLC_RESET_PORT, PLC_RESET_PIN);
        return(0);
    }
    if(plc_state.wait_t >= 1)
    {
        SET_PIN_H(PLC_RESET_PORT, PLC_RESET_PIN);
    }
    if(plc_state.wait_t >= MIN_PLC_RUN)
    {
        next_state();
    }
#endif
    return(0);
}
void init_frame_head(struct SHS_frame * pframe)
{
    pframe->stc = STC;
    memset_my(pframe->said, 0x00, ADDRESS_LEN);
    memset_my(pframe->taid, 0x11, ADDRESS_LEN);
    pframe->seq = 0;
}

static void send_local_frame(uint8 buffer[], uint8 len)
{
    struct SHS_frame *pframe = (struct SHS_frame *)buffer;

    memmove_my(&buffer[offset_of(struct SHS_frame, infor)], buffer, len);
    init_frame_head(pframe);
    pframe->length = len;
    pframe->infor[len] = checksum((uint8 *)pframe, SHS_FRAME_HEAD+len);

    uart_write(uart_socket, buffer, SHS_FRAME_HEAD+len+1);
}
#if 0
/************************************************* 
  �����ز�оƬͨ�Ų�����9600,8��n��1
  Baud (1byte��0x00�C2400��0x01�C4800��0x02 �C9600��0x03�C57600��0x04�C115200)
  Parity(1byte��0x00-��У�飬0x01�Cż���飬0x02 �C��У��)
*************************************************/
static void set_bps(uint8 buffer[])
{
    uint8 len=0;

    buffer[len++] = CMD_SET_BPS;
    buffer[len++] = 0x02;
    buffer[len++] = 0x01;
    send_local_frame(buffer, len);
}
//set plc uart bps
static uint8 wr_plc_bps(uint8 init, void *args)
{
    uint8 ret;
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if (init)
    {
        if (0x02 == eep_param.bps)
        {
            next_state();
            return(0);
        }
        set_bps(g_frame_buffer);
    }
    else
    {
        ret = local_ack_opt(pframe);
        if (0x01 == ret)
        {
            eep_param.bps = 0x02;
            EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
            SET_UART_BPS(9600);
        }
    }
    return(0);
}
#endif
/************************************************* 
  �����ز�оƬע������(1:����ע�᣻0������ע��)
*************************************************/
static void set_REG(uint8 buffer[])
{
    uint8 len=0;

    buffer[len++] = CMD_SET_REG;
    buffer[len++] = reg.type | reg.last_status;
    reg.last_status = 0;
    send_local_frame(buffer, len);
}

/************************************************* 
            ֪ͨ�ز�оƬ�Ͽ���·
*************************************************/
static void set_UNLINK(uint8 buffer[])
{
    buffer[0] = CMD_UNLINK;

    send_local_frame(buffer, 1);
}
/*************************************************
                �����ز�PANID
*************************************************/
static uint8 set_PANID(uint8 buffer[])
{
    uint8 len = 0;

    buffer[len++] = CMD_SET_PANID;
    //panid��־��Чͬʱpanidû�б����
    if(VALID_DATA != eep_param.panid_flag) return(0);
    mymemcpy(&buffer[len], eep_param.panid, PANID_LEN);
    len += PANID_LEN;

    send_local_frame(buffer, len);
    return(1);
}

static uint8 wr_plc_panid(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;
    uint8 ret;

    if(init) 
    {
        ret = set_PANID(g_frame_buffer);
        if(0x00 == ret) next_state();
    }
    else
    {
        local_ack_opt(pframe);
    }
    return(0);
}
/*************************************************
                �����ز�AID
*************************************************/
static void set_AID(uint8 buffer[])
{
    uint8 len = 0;

    buffer[len++] = CMD_SET_AID;
    mymemcpy(&buffer[len], eep_param.id, ID_LEN);
    len += ID_LEN;

    send_local_frame(buffer, len);
}

static uint8 wr_plc_aid(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(init)
    {
        set_AID(g_frame_buffer);
    }
    else
    {
        local_ack_opt(pframe);
    }
    return(0);
}

/*************************************************
�ϵ����ز�ģ��ͨ��һ�Σ��õ��ز�ģ���ַ�����ص�ַ 
type = MACID / GWID 
*************************************************/
void get_ID(uint8 type, uint8 buffer[])
{
    buffer[0] = type;

    send_local_frame(buffer, 1);
}
static uint8 rd_gw_aid(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(init)
    {
        get_ID(CMD_GET_GWAID,g_frame_buffer);
    }
    else
    {
        if((pframe != NULL) && (CMD_GET_GWAID == pframe->infor[0]))
        {
            if(0x00 != memcmp_my(eep_param.gateway_id, &pframe->infor[1], ID_LEN))
            {
                mymemcpy(eep_param.gateway_id, &pframe->infor[1], ID_LEN);
                //EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
            }
            next_state();
            return(0);
        }
        if(plc_state.wait_t > MAX_OVERTIME)
        {
 //           if(++plc_state.trycnt > MAX_TRYCNT) 
 //           {
 //               next_state();
 //               return(0);
 //           }
            try_state_again();
        }
    }
    return(0);
}

static uint8 rd_plc_sid(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(init)
    {
        get_ID(CMD_GET_SID,g_frame_buffer);
    }
    else
    {
        if((pframe != NULL) && (CMD_ACK_SID == pframe->infor[0]))
        {
            if(0x00 != memcmp_my(eep_param.sid, &pframe->infor[1], SID_LEN))
            {
                mymemcpy(eep_param.sid, &pframe->infor[1], SID_LEN);
                //EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
            }
            next_state();
            return(0);
        }
        if(plc_state.wait_t > MAX_OVERTIME)
        {
//            if(++plc_state.trycnt > MAX_TRYCNT) 
//            {
//                next_state();
//                return(0);
//            }
            try_state_again();
        }
    }
    return(0);
}

/*************************************************
                ��ȡ�ز�оƬEID
*************************************************/
uint8 check_sole_encode(struct EEP_PARAM *param)
{
#if 0
    struct ENCODE_PARAM encode_param;

    EEP_Read(ENCODE_MAGIC_ADDR,(uint8 *)&encode_param, sizeof(struct ENCODE_PARAM));
    if((0x00 == memcmp_my(param->id, encode_param.dev.id, ID_LEN))
       && (0x00 == memcmp_my(param->password, encode_param.dev.password, PWD_LEN))
       && (VALID_DATA == param->pwd_magic)) 
    {
        return(1);
    }

    if(ENCODE_MAGIC == encode_param.sole_magic) 
    {
        mymemcpy(param->id, encode_param.dev.id, ID_LEN);
        mymemcpy(param->password, encode_param.dev.password, PWD_LEN);
        param->pwd_magic = VALID_DATA;
        EEP_Write(OF_PLC_PARAM, (uint8 *)&param, sizeof(struct EEP_PARAM));
        return(1);
    }
#endif
    return(0);
}

static uint8 rd_plc_eid(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;
//    static uint8 reset_cnt=0;
//    const uint16 baud[]={2400, 4800, 9600};
//    static uint8 bps=0;

    if(init)         
    {
//        bps = eep_param.bps;
//        bps += plc_state.trycnt >> 1;
//        bps %= ARRAY_SIZE(baud);
//        SET_UART_BPS(baud[bps]);
	if(check_sole_encode(&eep_param)) 
	{
		next_state();
		return(0);
	}
        get_ID(CMD_GET_EID, g_frame_buffer);
    }
    else
    {
        if((pframe != NULL) && (CMD_ACK_EID == pframe->infor[0]))
        {
            if(0x00 != memcmp_my(eep_param.id, &pframe->infor[1], ID_LEN))
            {
                mymemcpy(eep_param.id, &pframe->infor[1], ID_LEN);
                //EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
            }
//            if(bps != eep_param.bps) eep_param.bps=0;//�������ز�ģ��,ԭ����9600bps,������!!
            next_state();
            return(0);
        }
        if(plc_state.wait_t > MAX_OVERTIME)
        {
//            if((reset_cnt < 1) && (++plc_state.trycnt > 6))
 //           {
//                reset_cnt++;
 //               chg_state(RST_PLC);
 //               return(0);
//            }
            try_state_again();
        }
    }

    return(0);
}

/************************************************ 
                    ����ע��
 ************************************************/
static uint8 set_unlink(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(init) 
    {
 //       if(VALID_DATA == eep_param.panid_flag)
 //       {
 //           EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
 //       }
        set_UNLINK(g_frame_buffer);
    }
    else
    {
        local_ack_opt(pframe);
    }
    return(0);
}
/**********************************************
�豸ע�᣺1����һ��ע�᲻ʹ�����룻 
          2���ڶ����Ժ�ע�ᣬ��ԭ��¼�����غ���ͬ��ʹ������
          3���ڶ����Ժ�ע�ᣬ��������ȡ�豸��Ҫʹ���豸����
***********************************************/
static uint8 get_reg(struct SHS_frame *pframe)
{
    uint8 need_wr=1;

    if(PASSWORD_REG == reg.type)
    {
        if(VALID_DATA != eep_param.panid_flag)
        {//��һ��ע�ᣬ���ж�����
        }
        else if((VALID_DATA == eep_param.panid_flag)
           && (0x00 == memcmp_my(eep_param.panid, &pframe->infor[1], PANID_LEN)))
        {//�ɹ�ע����ɺ��ٴ�����ע�ᣬֻ�ж�panid
            need_wr = 0;
        }
        else if((eep_param.pwd_magic == VALID_DATA)
                && (0x00 != memcmp_my(eep_param.password, &pframe->infor[PANID_LEN+1], PWD_LEN)))
        {//�ж��Ƿ����Ѿ�ע��ɹ���panid
            reg.last_status = PASSWORD_ERR;
            chg_state(UNLINK2);//20141018,���ز��Ͽ����ӻ��������,�޸��ز�����!!
            return(0);
        }
    }
    //�ɹ�����������ע��ɹ�

    reg.type = PASSWORD_REG;
    chg_state(G_GWID);//��ȡ����aid

    if(need_wr)
    {
        mymemcpy(eep_param.panid, &pframe->infor[1], PANID_LEN);
        eep_param.panid_flag = VALID_DATA;
        //EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
    }
    return(0);
}

static uint8 set_register(uint8 init, void *args)
{
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(init)
    {
        set_REG(g_frame_buffer);
    }
    else
    {
        local_ack_opt(pframe);
    }
    return(0);
}

/****************************************
            �ز���ʼ��״̬ת��
****************************************/

void plc_machine_opt(void *args)
{
    struct PLC_STATE *pstate = plc_state.pstate;
    uint8 init;

    init = plc_state.init;
    if(init) plc_state.wait_t=0;
    plc_state.init = 0;
    if(NULL != pstate)
    {
        pstate->action(init, args);
    }
}


/**************************************************************** 
                        ��֯��Ӧ����(��ӦPLCоƬ)
****************************************************************/
uint8 set_ret_frame(struct SHS_frame *pframe, uint8 len)
{
    //mymemcpy(pframe->taid, pframe->said, ID_LEN);  //tid and sid exchanged
    //mymemcpy(pframe->said, eep_param.id, ID_LEN);

    memset_my(pframe->said, 0x00, ADDRESS_LEN);
    memset_my(pframe->taid, 0x11, ADDRESS_LEN);

    pframe->seq |= 0x80;                         //�ظ����ı�־
    pframe->length = len;
	pframe->infor[len] = checksum((uint8 *)pframe, len +SHS_FRAME_HEAD);
    len++;

    /*the total bytes to be sent*/
    return(len + SHS_FRAME_HEAD);
}

/**************************************************************** 
                        ����ͨ��Э�����
****************************************************************/
void local_frame_opt(struct SHS_frame *pframe)
{
    if(CMD_REGINFOR == pframe->infor[0])
    {
        get_reg(pframe);
    }
    else if(CMD_REQ_AID == pframe->infor[0])
    {
        chg_state(S_AID);//�ز�����������ȡaid��˵���ز�оƬ���ܸ�λ����
    }
    else if(CMD_UNLINK == pframe->infor[0])
    {//���ضϿ����ӣ��ز�оƬ�ϱ��Ͽ����ӣ������ز�ע��panid���ã�������
        chg_state(S_PWDREG);
    }
//    else if(CMD_GET_GWAID == pframe->infor[0])
//    {
//        mymemcpy(eep_param.gateway_id, &pframe->infor[1], ID_LEN);
//        EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
//    }
}

/**************************************************************** 
                        MCU Update
****************************************************************/
struct UPDATE_FILE mcu_ota_tmp;
void initOtaTmp(struct UPDATE_FILE *updatefile)
{
    mymemcpy(mcu_ota_tmp.file_sz, updatefile->file_sz, 4);
    //mymemcpy(mcu_ota_tmp.ver, updatefile->ver, 2);
    mcu_ota_tmp.ver[0] = updatefile->ver[1];
    mcu_ota_tmp.ver[1] = updatefile->ver[0];
    mcu_ota_tmp.blk_sz = updatefile->blk_sz;
    mymemcpy(mcu_ota_tmp.type, updatefile->type, 8);
}

uint8 creatDataPacket(uint16 seq)
{
    flash_t flash;
    struct UPDATE *update;
    uint8 *data;
    int blk_sz;
    uint8 pac[SHS_FRAME_HEAD+DATA_PAC_LEN+1];

    pac[0] = CMD_UPDATE;
    update = (struct UPDATE *)&pac[1];
    data = &pac[1+UPDATE_LEN];
    
    //blk_sz = mcu_ota_tmp.blk_sz;
    blk_sz = OTA_BLK_SZ;
    mymemcpy(update->seq, &seq, 2);
    update->ack = 1;
    mymemcpy(update->crc, mcu_ota_tmp.ver, 2);
    update->len = blk_sz;

    flash_stream_read(&flash, MCU_OTA_DATA_ADDR+(seq-1)*blk_sz, blk_sz, data);
    send_local_frame(pac, DATA_PAC_LEN);
}

uint8 creatFristPacket()
{
    flash_t flash;
    struct UPDATE *update;
    struct UPDATE_FILE *updatefile;
    uint8 pac[SHS_FRAME_HEAD+FIRST_PAC_LEN+1];

    pac[0] = CMD_UPDATE;
    update = (struct UPDATE *)&pac[1];
    updatefile = (struct UPDATE_FILE *)&pac[1+UPDATE_LEN];

    memset_my(update->seq, 0, 2);
    update->ack = 1;
    memset_my(update->crc, 0, 2);
    update->len = UPDATE_FILE_LEN;

    flash_stream_read(&flash, MCU_OTA_ADDR, UPDATE_FILE_LEN, updatefile);
    initOtaTmp(updatefile);
    mymemcpy(update->crc, mcu_ota_tmp.ver, 2);
    mymemcpy(updatefile->ver, mcu_ota_tmp.ver, 2);
    updatefile->blk_sz = OTA_BLK_SZ;
    send_local_frame(pac, FIRST_PAC_LEN);
}
uint8 update_frame_opt(uint8 data[], uint8 len)
{
    uint8 ret = 0;
    uint16 seq;
    struct UPDATE *pupdate;
    pupdate = (struct UPDATE *)&data[0];
    
    if(len != pupdate->len+sizeof(struct UPDATE)) return(ret);
    seq = little_bytes_to_word(pupdate->seq);
    if (seq == 0)
    {
        return creatFristPacket();
    }
    else if (seq > 0 && seq != 0xFFFF)
    {
        return creatDataPacket(seq);
    }
    else if(seq == 0xFFFF)
    {
        return ret;
    }
}

/**************************************************************** 
                        ��أ�Զ�̣�ͨ��Э�����
****************************************************************/
uint8 remote_frame_opt(struct SHS_frame *pframe)
{
    uint8 ret = 0;

    //here we assume the pframe is g_frame_buffer,for the seek of little memory!!!
//    if ((NULL == pframe )||((char*)pframe !=(char*) g_frame_buffer))return(0);

    
    
    if(pframe->length < 1) return(0);	//Э�鱨�ĳ���0����Ӧ
    /*set command*/
    if (CMD_SET == (pframe->infor[0] & 0x07))
    {
        ret = set_parameter(&pframe->infor[1], pframe->length-1)+1;
    }
    /*read command*/
    else if (CMD_READ == (pframe->infor[0] & 0x07))
    {
        //ret = read_parameter(&pframe->infor[1], pframe->length-1)+1;
        ret = set_parameter(&pframe->infor[1], pframe->length-1)+1;
    }
    else if (CMD_UPDATE == (pframe->infor[0] & 0x07))
    {
        ret = update_frame_opt(&pframe->infor[1], pframe->length-1)+1;
        return(0);
    }
    else if (CMD_NOTIFY == (pframe->infor[0] & 0x07))
    {
        //ret = update_frame_opt(&pframe->infor[1], pframe->length-1)+1;
        ret = set_parameter(&pframe->infor[1], pframe->length-1)+1;
    }
    else if (CMD_ALARM == (pframe->infor[0] & 0x07))
    {
        //ret = update_frame_opt(&pframe->infor[1], pframe->length-1)+1;
        ret = set_parameter(&pframe->infor[1], pframe->length-1)+1;
    }
    return(0);
    //���б���
    if(pframe->seq & 0x80)
    {
        return(0);
    }
    return(ret);
}

uint8 frame_handle(uint8 init, void *args)
{
    uint8 len,ret;
    struct SHS_frame *pframe = (struct SHS_frame *)args;

    if(pframe == NULL) return(0);

    ret = remote_frame_opt(pframe);
    if (ret > 1)
    {
        len = set_ret_frame(pframe, ret);
        uart_write(uart_socket, (uint8 *)pframe, len);
    }
    
    return(0);
}

void setOutlet_onoff(uint8 cc, uint8 xx)
{
    uint8 buffer[6+SHS_FRAME_HEAD+1] = 
    {0x7e, 0x00, 0x00 , 0x00, 0x00 , 0x11, 0x11, 
    0x11 ,0x11 ,0x01 ,0x06, 0x07, 0x12, 0xC0, 0x02, 0x00, 0x00};
    buffer[15] = cc;
    buffer[16] = xx;
    buffer[6+SHS_FRAME_HEAD] = checksum((uint8 *)buffer, SHS_FRAME_HEAD+6);
    //send_local_frame(buffer, 6);
    //uart_write_my(buffer, SHS_FRAME_HEAD+6+1);
    uart_write(uart_socket, buffer, SHS_FRAME_HEAD+6+1);
}
void setMUC_date()
{
     uint8 buffer[11+SHS_FRAME_HEAD+1] = {0x07,0x11,0xC0,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
     struct tm timeinfo;
     
     if (!rtc_isenabled()) return;
     read_locoltime(&timeinfo);
     if (timeinfo.tm_year < 2000) return;
     buffer[10] = bin2bcd(timeinfo.tm_wday);
     buffer[9] = bin2bcd(timeinfo.tm_sec);
     buffer[8] = bin2bcd(timeinfo.tm_min);
     buffer[7] = bin2bcd(timeinfo.tm_hour);
     buffer[6] = bin2bcd(timeinfo.tm_mday);
     buffer[5] = bin2bcd(timeinfo.tm_mon);
     buffer[4] = bin2bcd(timeinfo.tm_year-2000);
     send_local_frame(buffer, 11);
}

void setOutlet_overvol(uint8 cc, void *args)
{
    uint8 buffer[9+SHS_FRAME_HEAD+1] = {0x07,0x11,0xB5,0x05,0x00,0x00,0x00,0x00,0x00};
    double overvol = *((double *)args);
    float undervol = (cc == 0x01)?(outletstateA.thrvol_under):(outletstateB.thrvol_under);
    
    buffer[4] = cc;
    //buffer[5] = bin2bcd(((int)(overvol*10))&0xFF);
  //  buffer[6] = bin2bcd(((int)(overvol/10))&0xFF);
 //   buffer[7] = bin2bcd(((int)(undervol*10))&0xFF);
//    buffer[8] = bin2bcd(((int)(undervol/10))&0xFF);

    numeric2bcd((uint32)(overvol*10), &buffer[5], 2);
    numeric2bcd((uint32)(undervol*10), &buffer[7], 2);

    //printf("setOutlet_overvol: %f  %d  0x%02x\n",overvol,a,b);
    send_local_frame(buffer, 9);
}

void setOutlet_undervol(uint8 cc, void *args)
{
    uint8 buffer[9+SHS_FRAME_HEAD+1] = {0x07,0x11,0xB5,0x05,0x00,0x00,0x00,0x00,0x00};
    double undervol = *((double *)args);
    float overvol = (cc == 0x01)?(outletstateA.thrvol_over):(outletstateB.thrvol_over);
    
    buffer[4] = cc;
    numeric2bcd((uint32)(overvol*10), &buffer[5], 2);
    numeric2bcd((uint32)(undervol*10), &buffer[7], 2);
    send_local_frame(buffer, 9);
}

void setOutlet_current(uint8 cc, void *args)
{
    uint8 buffer[8+SHS_FRAME_HEAD+1] = {0x07,0x21,0xB5,0x04,0x00,0x00,0x00,0x00};
    double current = *((double *)args);
    
    buffer[4] = cc;
    numeric2bcd((uint32)(current*1000), &buffer[5], 3);
    send_local_frame(buffer, 8);
}

void setOutlet_power(uint8 cc, void *args)
{
    uint8 buffer[8+SHS_FRAME_HEAD+1] = {0x07,0x30,0xB5,0x04,0x00,0x00,0x00,0x00};
    double power = *((double *)args);
    
    buffer[4] = cc;
    numeric2bcd((uint32)(power*10000), &buffer[5], 3);
    send_local_frame(buffer, 8);
}

void setOutlet_calibrate(uint8 cc, void *args)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x07,0xAA,0xCC,0x01,0x00};
    
    buffer[4] = cc;
    send_local_frame(buffer, 8);
}

void getMCU_ver()
{
    uint8 buffer[4+SHS_FRAME_HEAD+1] = {0x02,0x01,0x00,0x00};
    send_local_frame(buffer, 4);
}

void getMCU_onoff(uint8 cc)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x02,0x12,0xC0,0x01,0x00};
    buffer[4] = cc;
    send_local_frame(buffer, 5);
}

void getMCU_meterage(uint8 cc)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x02,0x3F,0xB5,0x01,0x00};
    buffer[4] = cc;
    send_local_frame(buffer, 5);
}

void getMCU_onoff_meter()
{
    uint8 buffer[61+SHS_FRAME_HEAD+1] = {0x02,\
    0x12,0xC0,0x02,0x01,0x00,0x12,0xC0,0x02,0x02,0x00,\
    0x3F,0xB5,0x02,0x01,0x00,0x3F,0xB5,0x02,0x02,0x00,\
    0x30,0xB5,0x02,0x01,0x00,0x30,0xB5,0x02,0x02,0x00,\
    0x21,0xB5,0x02,0x01,0x00,0x21,0xB5,0x02,0x02,0x00,\
    0x11,0xB5,0x02,0x01,0x00,0x11,0xB5,0x02,0x02,0x00,\
    0x10,0x90,0x02,0x01,0x00,0x10,0x90,0x02,0x02,0x00,\
    };
    send_local_frame(buffer, 61);
}
void setMCU_WAC_on()
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x07,0x06,0xC0,0x01,0x00};
    send_local_frame(buffer, 5);
}
void setMCU_WAC_off()
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x07,0x06,0xC0,0x01,0x01};
    send_local_frame(buffer, 5);
}
void getMCU_thrpow(uint8 cc)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x02,0x30,0xB5,0x01,0x00};
    buffer[4] = cc;
    send_local_frame(buffer, 5);
}

void getMCU_thrcur(uint8 cc)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x02,0x21,0xB5,0x01,0x00};
    buffer[4] = cc;
    send_local_frame(buffer, 5);
}

void getMCU_thrvol(uint8 cc)
{
    uint8 buffer[5+SHS_FRAME_HEAD+1] = {0x02,0x11,0xB5,0x01,0x00};
    buffer[4] = cc;
    send_local_frame(buffer, 5);
}

uint8 app_handle(uint8 code, void *args)
{
    switch(code)
    {
        case OUTLET_A_ON:
            setOutlet_onoff(0x01,0x01);
            break;
        case OUTLET_A_OFF:
            setOutlet_onoff(0x01,0x00);
            break;
        case OUTLET_B_ON:
            setOutlet_onoff(0x02,0x01);
            break;
        case OUTLET_B_OFF:
            setOutlet_onoff(0x02,0x00);
            break;
        case SET_DATE:
            setMUC_date();
            break;
        case OUTLET_A_OVERVOL:
            setOutlet_overvol(0x01, args);
            break;
        case OUTLET_B_OVERVOL:
            setOutlet_overvol(0x02, args);
            break;
        case OUTLET_A_UNDERVOL:
            setOutlet_undervol(0x01, args);
            break;
        case OUTLET_B_UNDERVOL:
            setOutlet_undervol(0x02, args);
            break;
        case OUTLET_A_CUR:
            setOutlet_current(0x01, args);
            break;
        case OUTLET_B_CUR:
            setOutlet_current(0x02, args);
            break;
        case OUTLET_A_POW:
            setOutlet_power(0x01, args);
            break;
        case OUTLET_B_POW:
            setOutlet_power(0x02, args);
            break;
        case OUTLET_A_CALIBRATE:
            setOutlet_calibrate(0x01, args);
            break;
        case OUTLET_B_CALIBRATE:
            setOutlet_calibrate(0x02, args);
            break;
        default:
            break;
    }
    return 0;
}

const struct PLC_STATE plc_state_slot[]=
{
    //init plc
    {RST_PLC,   R_EID,   reset_plc},
    {R_EID,     S_AID,   rd_plc_eid},
//    {S_BPS,     S_AID,   wr_plc_bps},
    {S_AID,     S_PWDREG,wr_plc_aid},
    {S_PWDREG,  S_PANID, set_register},
    {S_PANID,   _END,    wr_plc_panid},
    //key reg
#if KEY_REG
    {UNLINK1,   S_REG,   set_unlink},
#endif        
    //����ע�ᣬ�������������豸���벻ͬ���Ͽ��������Ӻ������ز�panid�������ز�ע��
    {UNLINK2,   PWD_ERR, set_unlink},
    {PWD_ERR,   S_PANID, set_register},

    //ע��ɹ�
    {G_GWID,    S_REG,   rd_gw_aid},
    {S_REG,     G_SID,   set_register},//����ע�����ԣ��ȴ��ز�оƬ�ϱ�panid
    {G_SID,     _END,    rd_plc_sid},

    {_END,      _END,    frame_handle},
};
#define   PLC_SLOT_SZ         (sizeof(plc_state_slot)/sizeof(plc_state_slot[0]))
#if DEBUG
//this is just for dubug purpose, no uart output
#define MAX_LOG_STATES     20
static uint8 state_logs[MAX_LOG_STATES*2];
static uint8 state_idx;
#endif
void chg_state(uint8 cur_state)
{
    uint8 i;
    static uint8 last_state=INVALID;
    
    plc_state.wait_t = 0;
    plc_state.init=1;
    for(i = 0; i < PLC_SLOT_SZ; i++)
    {
        if(plc_state_slot[i].cur_state == cur_state)
        {
            plc_state.pstate = (struct PLC_STATE *)&plc_state_slot[i];
            break;
        }
    }
    if(i >= PLC_SLOT_SZ)
    {
        plc_state.pstate = (struct PLC_STATE *)&plc_state_slot[0];
    }

    if(last_state != plc_state.pstate->cur_state)
    {
    #if DEBUG
        state_idx += 2;
        if(state_idx >= sizeof(state_logs)) state_idx=0;
    
        state_logs[state_idx] = plc_state.pstate->cur_state;
    #endif
        plc_state.trycnt = 0;
    }
    #if DEBUG
      state_logs[state_idx+1]++;
    #endif
    last_state = plc_state.pstate->cur_state;
    notify(EV_STATE);
}
/**************************************************** 
                    ͨ��Э�����
 ***************************************************/
void scan_uart_opt(void *args)
{
	struct SHS_frame *pframe;
	uint8  idx,len;
    
    idx = uart_peek_data(g_frame_buffer, sizeof(g_frame_buffer));

    pframe = get_smart_frame(g_frame_buffer,idx);

    if(NULL == pframe) return;

    idx = (uint8 *)pframe-g_frame_buffer;
    len = pframe->length+SHS_FRAME_HEAD+1;
    clear_uart(idx + len);
    memmove_my(&g_frame_buffer[0], &g_frame_buffer[idx], len);
    pframe = (struct SHS_frame *)g_frame_buffer;
    //����2Сʱ��ͨѶ��λ�ز�оƬ����
    clear_rst_time(pframe);
    plc_machine_opt(pframe);		    //protocol handle (buffer[i] is a complete frame data)
}

/****************************************************************** 
                    ��ʼ��ȫ�ֱ�����eeprom����
 *****************************************************************/
void system_init(void)
{
#if 0
    uint32 magic;

    //init eeprom param
    EEP_Read(OF_MAGIC_NUM, (uint8 *)&magic, OF_MAGIC_NUM_LEN);
    if(MAGIC_NUM != magic)
    {
        memset_my((uint8 *)&eep_param, 0x00, sizeof(eep_param));
//        memset_my(eep_param.password, 0xff, sizeof(eep_param.password));
        eep_param.pwd_magic = INVALID_DATA;
        EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
        magic = MAGIC_NUM;
        EEP_Write(OF_MAGIC_NUM, (uint8 *)&magic, OF_MAGIC_NUM_LEN);
    }
    EEP_Read(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
    /*��ʼ��״̬������*/
    memset_my(&plc_state, 0x00, sizeof(plc_state));
    memset_my(&reg, 0x00, sizeof(reg));
    reg.type = PASSWORD_REG;//�ϵ�����ע��
    chg_state(RST_PLC);
//    memset_my((uint8 *)&event, 0x00, sizeof(event));     //�¼���ʼ��
#if COLOR_DIMMER
    ram.color_change_flag=1;
#endif
#if 0
    if ((0xAA == ram.magic_ram)&&(ram.relay_stat&0x80))
    {
        if (ram.relay_stat&0x01) RELAY_ON(RELAY_CTRL1_PIN);
#if PWRCTRL_3
        if (ram.relay_stat&0x02) RELAY_ON(RELAY_CTRL2_PIN);
        if (ram.relay_stat&0x04) RELAY_ON(RELAY_CTRL3_PIN);
#endif
    }
    else
    {
        ram.magic_ram=0xAA;
        ram.relay_stat=0x00;
    }
#else
    #if PWRCTRL_86
        //�ϵ糣�պ�
        RELAY_ON(RELAY_CTRL1_PIN);
    #endif
#endif   
#endif
}
#if 0
/********************************************************* 
                         �¼��ϱ�
 ********************************************************/
void org_report_frame(uint8 type)
{

    struct SHS_frame *pshs_frame = (struct SHS_frame *)g_frame_buffer;
    struct FBD_Frame *pfbd_frame;
    uint8 len;

    pshs_frame->stc = STC;
    pshs_frame->seq = 1;
    mymemcpy(pshs_frame->said, eep_param.id, ID_LEN);
    mymemcpy(pshs_frame->taid, eep_param.gateway_id, ID_LEN);
    pfbd_frame = (struct FBD_Frame *)pshs_frame->infor;
    pfbd_frame->ctrl = 0x00;
    if(type & 0x01)
    {
        pfbd_frame->did[0] = 0x12;
        pfbd_frame->did[1] = 0xc0;
        _get_relay_status(pfbd_frame->data, 1);
        len = 1;
        pfbd_frame->ctrl += len;
    }
    else if(type & 0x02)
    {
        pfbd_frame->did[0] = 0x13;
        pfbd_frame->did[1] = 0xc0;
        len = 1;
        pfbd_frame->ctrl += len;
    }
    else
    {
        event.report = 0;
        return;
    }
    event.type &= ~type;
    pfbd_frame->data[len] = checksum((uint8 *)pshs_frame, len+SHS_FRAME_HEAD+FBD_FRAME_HEAD);
    len += SHS_FRAME_HEAD+FBD_FRAME_HEAD;
    len++;
    uart_write(g_frame_buffer, len);

}
#endif
#if 0
void event_report(void *args)
{
    if (0x00 == (event.report)) return;

    org_report_frame(event.type);
}
#endif
//��������
//2Сʱ�����κ��ز�ͨ�ţ���λ�ز�оƬ������
static void chk_plc_alive(void)
{
    if(rst_plc_t > 120)//2Сʱû��ͨ�ţ���λ�ز�оƬ
    {
        rst_plc_t = 0;
        chg_state(RST_PLC);
    }
    else if((60 == rst_plc_t) || (120 == rst_plc_t))
    {//��ȡ�ز�EID
        get_ID(CMD_GET_EID, g_frame_buffer);
    }
}
#if 1
void task_min(void *args)
{
    rst_plc_t++;

    chk_plc_alive();
#if 0
    if (PRESSKEY_REG == reg.type)
    {
        if (++reg.wait_t > 5)
        {//����ע�ᳬʱ
            reg.type = PASSWORD_REG;//�ָ�Ĭ������
            chg_state(S_REG);
        }
    }
#endif        
}
#endif
//������
void task_sec(void *args)
{
	//static uint8 cnt=0;

//	if(cnt++ >=5)
//	{
	//	cnt = 0;
//		GPIO_WriteReverse(RELAY_CTRL1_PORT,RELAY_CTRL1_PIN);
//	}
    plc_state.wait_t++;
}
void task_20ms(void *args)
{   
#if COLOR_DIMMER
  clour_dimmer_hook( );
#endif
}
//100msΪ��λ�İٺ�������
void task_100ms(void *args)
{
    notify(EV_STATE);
    uart_tick_hook();
#if KEY_REG
    chk_key_pressed();
#endif
#if DIMMER
    dimmer_100ms_hook( );
#endif
#if CURTAIN_MOTOR
    curtain_state_fun();
#endif
}

void clr_watchdog(void *args)
{
    //IWDG->KR = IWDG_KEY_REFRESH; 
}
#if KEY_REG
//����ע����ʼ״̬
void pressed_key(void *args)
{
    if (PRESSKEY_REG != reg.type)
    {
        reg.type = PRESSKEY_REG;
        reg.wait_t=0;
        chg_state(UNLINK1);
        if (INVALID_DATA == eep_param.panid_flag) return;
        eep_param.panid_flag = INVALID_DATA+0x80;
        EEP_Write(OF_PLC_PARAM, (uint8 *)&eep_param, sizeof(eep_param));
    }
}
#endif

void state_machine(void *args)
{
    plc_machine_opt(NULL);
}

void sys_tick(void *args)
{
}

const struct task tasks[] =
{
    {EV_RXCHAR, 0,            NULL,  scan_uart_opt},
    {EV_CLRDOG, ALWAYS_ALIVE, NULL,  clr_watchdog},
//    {EV_REPORT, 0,            NULL,  event_report},
    {EV_20MS,   0,            NULL,  task_20ms},
    {EV_100MS,  0,            NULL,  task_100ms},
    {EV_SEC,    0,            NULL,  task_sec},
#if KEY_REG
    {EV_KEY,    0,            NULL,  pressed_key},
#endif
    {EV_TICK,   ALWAYS_ALIVE, NULL,  sys_tick},
    {EV_MIN,    0,            NULL,  task_min},
    {EV_STATE,  0,            NULL,  state_machine},
};

void task_handle(void)
{
    uint8 i;
#if 0
    for (i = 0; i < ARRAY_SIZE(tasks); ++i)
    {
        if ((is_task_set(tasks[i].id))
            || (is_task_always_alive(tasks[i].flags)))
        {
            reset_task(tasks[i].id);
            tasks[i].handle(tasks[i].args);
        }
    }
   // wfi();
#endif
}



